#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "client.h"
#include <exception>

#include "sns.grpc.pb.h"
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using csce438::Message;
using csce438::ListReply;
using csce438::Request;
using csce438::Reply;
using csce438::Empty;
using csce438::ServerId;
using csce438::SNSService;
using csce438::RoutingService;

using namespace std;

std::string test_file = "";
bool first_time = true;

Message MakeMessage(const std::string& username, const std::string& msg) {
    Message m;
    m.set_username(username);
    m.set_msg(msg);
    google::protobuf::Timestamp* timestamp = new google::protobuf::Timestamp();
    timestamp->set_seconds(time(NULL));
    timestamp->set_nanos(0);
    m.set_allocated_timestamp(timestamp);
    return m;
}

class Client : public IClient
{
    public:
        Client(const std::string& hname,
               const std::string& uname,
               const std::string& p)
            :routing_hostname(hname), username(uname), routing_port(p)
            {}
    protected:
        virtual int connectTo();
        virtual IReply processCommand(std::string& input);
        virtual void processTimeline();
    private:
        std::string routing_hostname;
        std::string routing_port;
        std::string hostname;
        std::string username;
        std::string port;
        int server_pid;
        std::string hostname_slave;
        std::string port_slave;
        int server_pid_slave;
        std::unique_ptr<RoutingService::Stub> routing_stub;
        std::unique_ptr<SNSService::Stub> stub_;

        // Routing Service
        void ConnectToRoutingServer();
        int GetAvailableServer();
        int ConnectToAvailableServer();

        // SNS Service
        IReply runCommand(std::string& input);
        IReply Login();
        IReply List();
        IReply Follow(const std::string& username2);
        IReply UnFollow(const std::string& username2);
        void Timeline(const std::string& username);
};

int main(int argc, char** argv) {
    std::string routing_hostname = "localhost";
    std::string username = "default";
    std::string routing_port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "h:u:p:t:")) != -1){
        switch(opt) {
            case 'h':
                routing_hostname = optarg;break;
            case 'u':
                username = optarg;break;
            case 'p':
                routing_port = optarg;break;
            case 't':
                test_file = optarg;
                std::cout << "Stress Test Input File: " << test_file << std::endl;break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
        }
    }

    // Run client
    Client myc(routing_hostname, username, routing_port);

    myc.run_client();

    return 0;
}

int Client::connectTo()
{
    ConnectToRoutingServer();
    int grpc_status = -1;

    while (grpc_status != 1) {
        if (GetAvailableServer() == -1)
            return -1;
        grpc_status = ConnectToAvailableServer();

        // -1 signifies that there is no server currently available
        if (server_pid == -1) {
            grpc_status = -1;
            continue;
        }
    }

    return 1;
}

void Client::ConnectToRoutingServer() {
    std::string login_info = routing_hostname + ":" + routing_port;
    routing_stub = std::unique_ptr<RoutingService::Stub>(RoutingService::NewStub(
        grpc::CreateChannel(login_info, grpc::InsecureChannelCredentials())));
}

int Client::GetAvailableServer() {

  if(first_time)
  {
    ClientContext context;
    Empty empty;
    ServerId id;

    Status status = routing_stub->GetAvailableServer(&context, empty, &id);
    if(!status.ok()) {
        return -1;
    }

    hostname = id.hostname();
    port = id.port();
    server_pid = id.pid();

    hostname_slave = id.hostname_slave();
    port_slave = id.port_slave();
    server_pid_slave = id.pid_slave();

    cout << "MASTER SERVER: \t" << hostname + ":" + port << endl;
    cout << "SLAVE SERVER: \t" << hostname_slave + ":" + port_slave << endl << endl;

    first_time = false;
    return 1;
  }

  std::string login_info = hostname + ":" + port;
  stub_ = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(
      grpc::CreateChannel(login_info, grpc::InsecureChannelCredentials())));

  std::string slave_login_info = hostname_slave + ":" + port_slave;
  auto stub_slave = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(
      grpc::CreateChannel(slave_login_info, grpc::InsecureChannelCredentials())));

  ClientContext context_for_master;
  ClientContext context_for_slave;
  Empty empty1;
  Empty empty2;

  Status status_of_master = stub_->CheckServer(&context_for_master, empty1, &empty2);
  Status status_of_slave = stub_slave->CheckServer(&context_for_slave, empty1, &empty2);

  if(status_of_master.ok())
  {
    return 1;
  }
  else if(status_of_slave.ok())
  {
    cout << "\nMaster server found dead, but slave is ok.  Reconnecting... \n\n";
    hostname = hostname_slave;
    port = port_slave;
    server_pid = server_pid_slave;
  }
  else
  {
    cout << "\nMaster server and Slave server are found dead, or never assigned.  Contacting Router... \n\n";
    ClientContext context;
    Empty empty;
    ServerId id;

    Status status = routing_stub->GetAvailableServer(&context, empty, &id);
    if(!status.ok()) {
        return -1;
    }

    hostname = id.hostname();
    port = id.port();
    server_pid = id.pid();

    hostname_slave = id.hostname_slave();
    port_slave = id.port_slave();
    server_pid_slave = id.pid_slave();

    cout << "MASTER SERVER: \t" << hostname + ":" + port << endl;
    cout << "SLAVE SERVER: \t" << hostname_slave + ":" + port_slave << endl << endl;


    return 1;
  }
}

int Client::ConnectToAvailableServer() {
    std::string login_info = hostname + ":" + port;
    stub_ = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(
        grpc::CreateChannel(login_info, grpc::InsecureChannelCredentials())));

    IReply ire = Login();
    if(!ire.grpc_status.ok()) {
        return -1;
    }
    return 1;
}


IReply Client::processCommand(std::string& input)
{
    IReply ire = runCommand(input);
    while (!ire.grpc_status.ok()) {

        // Report bad server
        ClientContext context;
        ServerId id;
        id.set_pid(server_pid);
        id.set_hostname(hostname);
        id.set_port(port);
        Empty empty;
        routing_stub->RemoveServer(&context, id, &empty);

        // get new available server info from routing server
        GetAvailableServer();

        // connect to new available server
        ConnectToAvailableServer();

        // resend command
        ire = runCommand(input);
    }
    return ire;
}

IReply Client::runCommand(std::string& input)
{
    IReply ire;
    std::size_t index = input.find_first_of(" ");
    if (index != std::string::npos) {
        std::string cmd = input.substr(0, index);
        /*
        if (input.length() == index + 1) {
            std::cout << "Invalid Input -- No Arguments Given\n";
        }
        */
        std::string argument = input.substr(index+1, (input.length()-index));

        if (cmd == "FOLLOW") {
            return Follow(argument);
        } else if(cmd == "UNFOLLOW") {
            return UnFollow(argument);
        }
    } else {
        if (input == "LIST") {
            return List();
        } else if (input == "TIMELINE") {
            ire.comm_status = SUCCESS;
            return ire;
        }
    }

    ire.comm_status = FAILURE_INVALID;
    return ire;
}

void Client::processTimeline()
{
    Timeline(username);
}

IReply Client::List() {
    //Data being sent to the server
    Request request;
    request.set_username(username);

    //Container for the data from the server
    ListReply list_reply;

    //Context for the client
    ClientContext context;
    Status status = stub_->List(&context, request, &list_reply);

    IReply ire;
    ire.grpc_status = status;
    //Loop through list_reply.all_users and list_reply.following_users
    //Print out the name of each room
    if(status.ok()){
        ire.comm_status = SUCCESS;
        for(std::string s : list_reply.all_users()){
            ire.all_users.push_back(s);
        }
        for(std::string s : list_reply.followers()){
            ire.followers.push_back(s);
        }
    }
    return ire;
}

IReply Client::Follow(const std::string& username2) {
    Request request;
    request.set_username(username);
    request.add_arguments(username2);

    Reply reply;
    ClientContext context;

    Status status = stub_->Follow(&context, request, &reply);
    IReply ire; ire.grpc_status = status;
    if (reply.msg() == "Follow Failed -- Invalid Username") {
        ire.comm_status = FAILURE_INVALID_USERNAME;
    } else if (reply.msg() == "Follow Failed -- Already Following User") {
        ire.comm_status = FAILURE_ALREADY_EXISTS;
    } else if (reply.msg() == "Follow Successful") {
        ire.comm_status = SUCCESS;
    } else {
        ire.comm_status = FAILURE_UNKNOWN;
    }
    return ire;
}

IReply Client::UnFollow(const std::string& username2) {
    Request request;

    request.set_username(username);
    request.add_arguments(username2);

    Reply reply;

    ClientContext context;

    Status status = stub_->UnFollow(&context, request, &reply);
    IReply ire;
    ire.grpc_status = status;
    if (reply.msg() == "UnFollow Failed -- Invalid Username") {
        ire.comm_status = FAILURE_INVALID_USERNAME;
    } else if (reply.msg() == "UnFollow Failed -- Not Following User") {
        ire.comm_status = FAILURE_INVALID_USERNAME;
    } else if (reply.msg() == "UnFollow Successful") {
        ire.comm_status = SUCCESS;
    } else {
        ire.comm_status = FAILURE_UNKNOWN;
    }

    return ire;
}

IReply Client::Login() {
    Request request;
    request.set_username(username);
    Reply reply;
    ClientContext context;

    Status status = stub_->Login(&context, request, &reply);

    IReply ire;
    ire.grpc_status = status;
    if (reply.msg() == "Invalid Username") {
        ire.comm_status = FAILURE_ALREADY_EXISTS;
    } else {
        ire.comm_status = SUCCESS;
    }
    return ire;
}

void Client::Timeline(const std::string& username) {
    while (true) {
        GetAvailableServer();
        ConnectToAvailableServer();

        ClientContext context;
        std::shared_ptr<ClientReaderWriter<Message, Message>> stream(
                stub_->Timeline(&context));

        //Thread used to read chat messages and send them to the server
        std::thread writer([username, stream]() {
                std::string input = "Set Stream";
                Message m = MakeMessage(username, input);
                stream->Write(m);
                while (1) {
                    input = getPostMessage();
                    m = MakeMessage(username, input);
                    stream->Write(m);
                }
                stream->WritesDone();
            });

        std::thread reader([username, stream]() {
                Message m;
                while(stream->Read(&m)){
                    google::protobuf::Timestamp temptime = m.timestamp();
                    std::time_t time = temptime.seconds();
                    displayPostMessage(m.username(), m.msg(), time);
                }
            });

        //If proceeding beyond this join, the server probably crashed
        reader.join();
    }


}
