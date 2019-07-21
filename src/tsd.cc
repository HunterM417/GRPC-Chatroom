/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *         * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *         * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *         * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <bits/stdc++.h>
#include <map>
#include <chrono>

#include "sns.grpc.pb.h"

using csce438::ListReply;
using csce438::Message;
using csce438::Reply;
using csce438::Request;
using csce438::Empty;
using csce438::ServerId;
using csce438::ServerIds;

using csce438::SNSService;
using csce438::RoutingService;

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

struct Client
{
    string username;
    bool connected = true;
    int following_file_size = 0;
    vector<Client *> client_followers;
    vector<Client *> client_following;
    ServerReaderWriter<Message, Message> *stream = 0;
    bool operator==(const Client &c1) const
    {
        return (username == c1.username);
    }
};

string routing_hostname;
string routing_port;
string hostname;
string port;
unique_ptr<RoutingService::Stub> routing_stub;

//Vector that stores every client that has been created
vector<Client *> client_db;


//Helper function used to find a Client object given its username
int find_user(string username)
{
    int index = 0;
    for (Client *c : client_db)
    {
        if (c->username == username)
            return index;
        index++;
    }
    return -1;
}

void update_stream(string username, ServerReaderWriter<Message, Message> *stream)
{
    /*
    for (Client* c : client_db) {
        if (username == c->username)
            c->stream = stream;
        else {
            for (Client f : c->client_followers) {

            }
        }
    }
    */
}

void write_userlist()
{
    string filename = "userlist.txt";
    ofstream user_file(filename, ios::trunc | ios::out | ios::in);
    for (Client *c : client_db)
    {
        user_file << c->username << " " << c->following_file_size << endl;
        vector<Client *>::const_iterator it;
        for (it = c->client_followers.begin(); it != c->client_followers.end(); it++)
        {
            user_file << (*it)->username << " ";
        }
        user_file << endl;
        for (it = c->client_following.begin(); it != c->client_following.end(); it++)
        {
            user_file << (*it)->username << " ";
        }
        user_file << endl;
    }
}

void read_userlist()
{
    ifstream in("userlist.txt");
    map<string, vector<string>> followers;
    map<string, vector<string>> followings;

    string line1;
    while (getline(in, line1))
    {
        Client *c = new Client();
        stringstream s_line1(line1);
        string username;
        string file_size;

        getline(s_line1, username, ' ');
        getline(s_line1, file_size, ' ');
        c->username = username;
        c->following_file_size = atoi(file_size.c_str());
        client_db.push_back(c);

        string line2, line3, item;
        vector<string> v_followers, v_followings;
        getline(in, line2);
        stringstream s_line2(line2);
        while (getline(s_line2, item, ' '))
        {
            v_followers.push_back(item);
        }
        followers[username] = v_followers;

        getline(in, line3);
        stringstream s_line3(line3);
        while (getline(s_line3, item, ' '))
        {
            v_followings.push_back(item);
        }
        followings[username] = v_followings;
    }

    map<string, vector<string>>::iterator it;
    for (it = followers.begin(); it != followers.end(); it++)
    {
        string username = it->first;
        vector<string> v_followers = it->second;
        Client *user = client_db[find_user(username)];

        vector<string>::const_iterator itt;
        for (itt = v_followers.begin(); itt != v_followers.end(); itt++)
        {
            user->client_followers.push_back(client_db[find_user(*itt)]);
        }
    }
    for (it = followings.begin(); it != followings.end(); it++)
    {
        string username = it->first;
        vector<string> v_followings = it->second;
        Client *user = client_db[find_user(username)];

        vector<string>::const_iterator itt;
        for (itt = v_followings.begin(); itt != v_followings.end(); itt++)
        {
            user->client_following.push_back(client_db[find_user(*itt)]);
        }
    }
}

void ConnectToRoutingServer() {
    string login_info = routing_hostname + ":" + routing_port;
    cout << "Connecting to routing server at " + login_info + "\n";
    routing_stub = unique_ptr<RoutingService::Stub>(RoutingService::NewStub(
        grpc::CreateChannel(login_info, grpc::InsecureChannelCredentials())));

    ClientContext context;
    Empty empty;
    ServerId id;
    id.set_pid((int)getpid());
    id.set_hostname(hostname);
    id.set_port(port);

    Status status = routing_stub->AddServer(&context, id, &empty);
}

int SetAvailableServer() {
    ClientContext context;
    ServerId id;
    id.set_pid(getpid());
    id.set_hostname(hostname);
    id.set_port(port);
    Empty empty;

    routing_stub->SetAvailableServer(&context, id, &empty);
    cout << "Elected self as available server!\n";
}

int RunElection() {
    ClientContext context;
    Empty empty;
    ServerIds ids;
    Status status = routing_stub->GetServerIds(&context, empty, &ids);

    // If this is the only server, set it as the available server
    if (ids.ids_size() == 1) {
        cout << "This is the only server connected to the routing server!\n";
        return SetAvailableServer();
    }

    // Send election checks to all servers with greater pids
    int num_larger_ids = 0;
    int num_bad_replies = 0;
    for (auto id : ids.ids()) {
        if (id.pid() < getpid())
            continue;

        if (id.pid() == getpid() && id.port() == port && id.hostname() == hostname)
            continue;

        num_larger_ids++;

        auto stub = unique_ptr<SNSService::Stub>(SNSService::NewStub(
            grpc::CreateChannel(id.hostname() + ":" + id.port(), grpc::InsecureChannelCredentials())));

        ClientContext context;
        Empty empty1;
        Empty empty2;
        status = stub->HoldElection(&context, empty1, &empty2);

        if (!status.ok()) {
            num_bad_replies++;

            // Remove bad server from server list on routing server
            cout << "Removing bad server " + id.hostname() +  ":" + id.port() + "\n";
            ClientContext context;
            Empty empty;
            routing_stub->RemoveServer(&context, id, &empty);
        }
    }

    // If the number of messages sent out equals the number of non-replies
    // then this server should be set as the available server
    if (num_larger_ids == num_bad_replies) {
        cout << "num larger ids = " << num_larger_ids << endl;
        cout << "num bad replies = " << num_bad_replies << endl;
        return SetAvailableServer();
    }

    return 0;
}

class SNSServiceImpl final : public SNSService::Service
{
    Status List(ServerContext *context, const Request *request, ListReply *list_reply) override
    {
        Client *user = client_db[find_user(request->username())];
        int index = 0;
        for (Client *c : client_db)
        {
            list_reply->add_all_users(c->username);
        }
        vector<Client *>::const_iterator it;
        for (it = user->client_followers.begin(); it != user->client_followers.end(); it++)
        {
            list_reply->add_followers((*it)->username);
        }
        return Status::OK;
    }

    Status Follow(ServerContext *context, const Request *request, Reply *reply) override
    {
        string username1 = request->username();
        string username2 = request->arguments(0);
        int join_index = find_user(username2);
        if (join_index < 0 || username1 == username2)
            reply->set_msg("Follow Failed -- Invalid Username");
        else
        {
            Client *user1 = client_db[find_user(username1)];
            Client *user2 = client_db[join_index];
            if (find(user1->client_following.begin(), user1->client_following.end(), user2) != user1->client_following.end())
            {
                reply->set_msg("Follow Failed -- Already Following User");
                return Status::OK;
            }
            user1->client_following.push_back(user2);
            user2->client_followers.push_back(user1);
            reply->set_msg("Follow Successful");

            for (Client *c : client_db)
            {
                string filename = c->username + "followers.txt";
                ofstream user_file(filename, ios::trunc | ios::out | ios::in);
                vector<Client *>::const_iterator it;
                for (it = c->client_followers.begin(); it != c->client_followers.end(); it++)
                {
                    user_file << (*it)->username << endl;
                }
            }
            write_userlist();
        }
        return Status::OK;
    }

    Status UnFollow(ServerContext *context, const Request *request, Reply *reply) override
    {
        string username1 = request->username();
        string username2 = request->arguments(0);
        int leave_index = find_user(username2);
        if (leave_index < 0 || username1 == username2)
            reply->set_msg("UnFollow Failed -- Invalid Username");
        else
        {
            Client *user1 = client_db[find_user(username1)];
            Client *user2 = client_db[leave_index];
            if (find(user1->client_following.begin(), user1->client_following.end(), user2) == user1->client_following.end())
            {
                reply->set_msg("UnFollow Failed -- Not Following User");
                return Status::OK;
            }
            user1->client_following.erase(find(user1->client_following.begin(), user1->client_following.end(), user2));
            user2->client_followers.erase(find(user2->client_followers.begin(), user2->client_followers.end(), user1));
            reply->set_msg("UnFollow Successful");
            for (Client *c : client_db)
            {
                string filename = c->username + "followers.txt";
                ofstream user_file(filename, ios::trunc | ios::out | ios::in);
                vector<Client *>::const_iterator it;
                for (it = c->client_followers.begin(); it != c->client_followers.end(); it++)
                {
                    user_file << (*it)->username << endl;
                }
            }
            write_userlist();
        }
        return Status::OK;
    }

    Status Login(ServerContext *context, const Request *request, Reply *reply) override
    {
        Client *c = new Client();
        string username = request->username();
        int user_index = find_user(username);
        if (user_index < 0)
        {
            c->username = username;
            c->client_followers.push_back(c);
            c->client_following.push_back(c);
            client_db.push_back(c);
            reply->set_msg("Login Successful!");
        }
        else
        {
            Client *user = client_db[user_index];
            if (user->connected)
            {
                reply->set_msg("Invalid Username");
                return Status::OK;
            }
            else
            {
                string msg = "Welcome Back " + user->username;
                reply->set_msg(msg);
                user->connected = true;
            }
        }
        write_userlist();
        return Status::OK;
    }

    Status Timeline(ServerContext *context,
                    ServerReaderWriter<Message, Message> *stream) override
    {
        Message message;
        Client *c;
        while (stream->Read(&message))
        {
            string username = message.username();
            int user_index = find_user(username);
            c = client_db[user_index];

            //Write the current message to "username.txt"
            string filename = username + ".txt";
            ofstream user_file(filename, ios::app | ios::out | ios::in);
            google::protobuf::Timestamp temptime = message.timestamp();
            string time = google::protobuf::util::TimeUtil::ToString(temptime);
            string fileinput = time + " :: " + message.username() + ":" + message.msg() + "\n";
            //"Set Stream" is the default message from the client to initialize the stream
            if (message.msg() != "Set Stream")
                user_file << fileinput;
            //If message = "Set Stream", print the first 20 chats from the people you follow
            else
            {
                if (c->stream == 0)
                {
                    c->stream = stream;
                    update_stream(c->username, stream);
                }
                string line;
                vector<string> newest_twenty;
                ifstream in(username + "following.txt");
                int count = 0;
                //Read the last up-to-20 lines (newest 20 messages) from userfollowing.txt
                while (getline(in, line))
                {
                    if (c->following_file_size > 20)
                    {
                        if (count < c->following_file_size - 20)
                        {
                            count++;
                            continue;
                        }
                    }
                    newest_twenty.push_back(line);
                }
                Message new_msg;
                //Send the newest messages to the client to be displayed
                for (int i = 0; i < newest_twenty.size(); i++)
                {
                    new_msg.set_msg(newest_twenty[i]);
                    stream->Write(new_msg);
                }
                continue;
            }
            //Send the message to each follower's stream
            vector<Client *>::const_iterator it;
            for (it = c->client_followers.begin(); it != c->client_followers.end(); it++)
            {
                Client *temp_client = *it;
                if (temp_client->stream != 0 && temp_client->connected && temp_client->username != c->username)
                {
                    message.set_msg(fileinput);
                    temp_client->stream->Write(message);
                }
                //For each of the current user's followers, put the message in their following.txt file
                string temp_username = temp_client->username;
                string temp_file = temp_username + "following.txt";
                ofstream following_file(temp_file, ios::app | ios::out | ios::in);
                following_file << fileinput;
                temp_client->following_file_size++;
                ofstream user_file(temp_username + ".txt", ios::app | ios::out | ios::in);
                user_file << fileinput;
                write_userlist();
            }
        }
        //If the client disconnected from Chat Mode, set connected to false
        c->connected = false;
        return Status::OK;
    }

    Status CheckServer(ServerContext *context, const Empty *empty1, Empty *empty2) override
    {
        return Status::OK;
    }

    Status HoldElection(ServerContext *context, const Empty *empty1, Empty *empty2) override
    {
        cout << "Holding Election!\n";
        RunElection();
        return Status::OK;
    }
};

void RunServer()
{
    string server_address = "0.0.0.0:" + port;
    SNSServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    unique_ptr<Server> server(builder.BuildAndStart());
    cout << "Server listening on " << server_address << endl;

    server->Wait();
}

void StartMaster() {
    cout << "PID = " << getpid() << endl;
    read_userlist();

    ConnectToRoutingServer();
    RunElection();
    RunServer();
}

void MonitorMasterServer(pid_t master_pid) {
    cout << "Starting slave process\n";

    while (true) {
        if (0 != kill(master_pid, 0)) {
            cout << "Restarting master...\n";

            master_pid = getpid();
            pid_t pid = fork();
            if (pid > 0) {
                break;
            }
            else if (pid == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            else {
                cout << "fork() failed!\n";
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // Restart master server
    StartMaster();
}

int main(int argc, char **argv)
{
    routing_hostname =  "localhost";
    routing_port = "3010";

    hostname =  "localhost";
    port = "3010";

    int opt = 0;
    while ((opt = getopt(argc, argv, "h:p:r:t:f:")) != -1)
    {
        switch (opt)
        {
        case 'h':
            hostname = optarg;
            break;
        case 'p':
            port = optarg;
            break;
        case 'r':
            routing_hostname = optarg;
            break;
        case 't':
            routing_port = optarg;
            break;
        default:
            cerr << "Invalid Command Line Argument\n";
        }
    }

    pid_t master_pid = getpid();
    pid_t pid = fork();
    if (pid == 0) {
        // child process
        MonitorMasterServer(master_pid);
    }
    else if (pid > 0) {
        // parent process
        StartMaster();
    }
    else {
        // fork failed
        cout << "fork() failed!\n";
        return 1;
    }

    return 0;
}
