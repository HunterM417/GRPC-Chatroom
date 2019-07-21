#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <bits/stdc++.h>
#include <set>
#include <thread>
#include <chrono>

#include "sns.grpc.pb.h"

using csce438::ListReply;
using csce438::Message;
using csce438::Reply;
using csce438::Request;
using csce438::Empty;
using csce438::ServerId;
using csce438::ServerIds;
using csce438::RoutingService;
using csce438::SNSService;
using google::protobuf::Duration;
using google::protobuf::Timestamp;

using grpc::Channel;
using grpc::ClientContext;

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

using namespace std;

struct MasterServer {
    int pid;
    string hostname;
    string port;

    int client_count;

    MasterServer() {}
    MasterServer(const ServerId *id) : pid(id->pid()), hostname(id->hostname()), port(id->port()), client_count(0) {}

    ServerId getServerId() {
        ServerId id;
        id.set_pid(pid);
        id.set_hostname(hostname);
        id.set_port(port);
        return id;
    }

    string stringify() {
        return to_string(pid) + ", " + hostname + ":" + port;
    }

    bool operator==(const MasterServer &other) const
    {
        return (
            pid == other.pid &&
            hostname == other.hostname &&
            port == other.port);
    }

    bool operator<(const MasterServer &other) const
	{
        string hash = hostname + ":" + port + ":" + to_string(pid);
        string other_hash = other.hostname + ":" + other.port + ":" + to_string(other.pid);
        return hash < other_hash;
	}
};

MasterServer available_server;
MasterServer best_master_server;
MasterServer best_slave_server;
set<MasterServer> servers;

bool CheckServerHealth(MasterServer server) {
    auto stub = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(grpc::CreateChannel(
        server.hostname + ":" + server.port, grpc::InsecureChannelCredentials())));
    ClientContext context;
    Empty empty1;
    Empty empty2;

    Status status = stub->CheckServer(&context, empty1, &empty2);
    return status.ok();
}

void StartElectionProcess() {
    cout << "Asking random server to begin election process.\n";
    for (auto it = servers.begin(); it != servers.end(); it++) {
        cout << "\t" << it->pid << ", " << it->hostname + ":" + it->port + "\n";
        if (!CheckServerHealth(*it)) {
            it = servers.erase(it);
            continue;
        }

        auto stub = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(grpc::CreateChannel(
            it->hostname + ":" + it->port, grpc::InsecureChannelCredentials())));
        ClientContext context;
        Empty empty1;
        Empty empty2;

        Status status = stub->HoldElection(&context, empty1, &empty2);
        if (status.ok())
            break;
    }
}

class RoutingServiceImpl final : public RoutingService::Service
{
    Status GetAvailableServer(ServerContext *context, const Empty *empty, ServerId *id) override
    {
        if (servers.size() == 0) {
            id->set_pid(-1);
            id->set_hostname("0.0.0.0");
            id->set_port("0");
            return Status::OK;
        }

        int best_master_server_id = rand() % servers.size();
        set<MasterServer>::const_iterator it(servers.begin());

        advance(it, best_master_server_id);

        best_master_server = *it;
        advance(it, 1);
        if(it == servers.end())
        {
          best_slave_server = *(servers.begin());
        }
        else
        {
          best_slave_server = *it;
        }

        id->set_pid(best_master_server.pid);
        id->set_hostname(best_master_server.hostname);
        id->set_port(best_master_server.port);
        id->set_pid_slave(best_slave_server.pid);
        id->set_hostname_slave(best_slave_server.hostname);
        id->set_port_slave(best_slave_server.port);

        return Status::OK;
    }

    Status SetAvailableServer(ServerContext *context, const ServerId *id, Empty *empty) override
    {
        available_server = MasterServer(id);
        return Status::OK;
    }

    Status AddServer(ServerContext *context, const ServerId *id, Empty *empty) override
    {
        MasterServer server(id);

        if (servers.size() == 0)
            available_server = server;

        servers.insert(server);

        return Status::OK;
    }

    Status RemoveServer(ServerContext *context, const ServerId *id, Empty *empty) override
    {
        MasterServer server(id);
        cout << "Removing server " + server.stringify() << endl;
        servers.erase(server);
        return Status::OK;
    }

    Status GetServerIds(ServerContext *context, const Empty *empty, ServerIds *ids) override
    {
        for (auto server : servers) {
            ServerId *newid = ids->add_ids();
            newid->set_pid(server.pid);
            newid->set_hostname(server.hostname);
            newid->set_port(server.port);
        }
        return Status::OK;
    }
};

void MonitorConnectedServers() {
    while(true) {
      printf("\033[2J\033[1;1H");
      cout << setw(37) << "Routing Server"  << endl;
	    cout<< "----------------------------------------------------------" << endl;
      cout << "List of Healthy Servers:\n";
        for (auto server : servers) {
            if (!CheckServerHealth(server)) {
                cout << "Removing unhealthy server " + server.stringify() << endl;
                servers.erase(server);

                if (server == available_server && servers.size() > 0) {
                    cout << "Just removed unhealthy available server\n";
                    StartElectionProcess();
                }
                break;
            }
            cout << "PID: " << server.pid << "\tServer: " + server.hostname + ":" + server.port << "\n";
        }
        cout<< "----------------------------------------------------------" << endl;
        cout << "*\tNumber of servers remaining: " << servers.size() << endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void RunServer(string port_no)
{
    string server_address = "0.0.0.0:" + port_no;
    RoutingServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    unique_ptr<Server> server(builder.BuildAndStart());

    std::thread monitor_thread(MonitorConnectedServers);
    server->Wait();
    monitor_thread.join();
}

int main(int argc, char **argv)
{
    string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "p:")) != -1)
    {
        switch (opt)
        {
        case 'p':
            port = optarg;
            break;
        default:
            cerr << "Invalid Command Line Argument\n";
        }
    }

    RunServer(port);
    return 0;
}
