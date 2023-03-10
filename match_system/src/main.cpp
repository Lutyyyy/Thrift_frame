// This autogenerated skeleton file illustrates how to build a server.
// You should copy it to another filename to avoid overwriting it.

#include "match_server/Match.h"
#include "save_client/Save.h"
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/ThreadFactory.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/TToString.h>

#include <iostream>
#include <thread>
#include <condition_variable>
#include <queue>
#include <vector>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using namespace ::match_service;
using namespace ::save_service;

struct Task {
    User user;
    std::string operation_type;
};

struct MessageQueue {
    std::queue<Task> q;
    std::mutex m;
    std::condition_variable cv;
} message_queue;

class Pool {
    public:
        // add to the match pools
        void add(User user) {
            users.push_back(user);
            waiting_time.push_back(0);
        }

        // remove from the match pools
        void remove(User user) {
            for (uint32_t i = 0; i < users.size(); i ++)
                if (users[i] == user) {
                    users.erase(users.begin() + i);
                    waiting_time.erase(waiting_time.begin() + i);
                    break;
                }
        }

        bool check_match(uint32_t i, uint32_t j) {
            auto a = users[i], b = users[j];
            int diff_time = abs(a.score - b.score);
            int a_max_interval = waiting_time[i] * 50;
            int b_max_interval = waiting_time[j] * 50;

            return diff_time <= a_max_interval && diff_time <= b_max_interval;
        }

        // match func
        void match() {
            // waiting time ++
            for (auto t : waiting_time) t ++;

            while (users.size() > 1) {
                bool flag = true;
                for (uint32_t i = 0; i < users.size(); i ++) {
                    for (uint32_t j = i + 1; j < users.size(); j ++) {
                        if (check_match(i, j)) {
                            auto a = users[i], b = users[j];
                            users.erase(users.begin() + j);
                            users.erase(users.begin() + i);
                            waiting_time.erase(waiting_time.begin() + j);
                            waiting_time.erase(waiting_time.begin() + i);
                            save_reslut(a.id, b.id);
                            flag = false;
                            break;
                        }
                    }
                    if (!flag) break;
                }
                if (flag) break;
            }
        }

        // save match results
        void save_reslut(uint32_t a, uint32_t b) {
            printf("Match result is: %d %d\n", a, b);

            std::shared_ptr<TTransport> socket(new TSocket("47.113.xxx.xxx", 9090)); // change to myserver
            std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
            std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
            SaveClient client(protocol);

            try {
                transport->open();

                client.save_data("my_server_username", "my_password_md5sum", a, b);

                transport->close();
            }
            catch (TException& tx) {
                std::cout << "ERROR: " << tx.what() << '\n';
            }
        }

    private:
        std::vector<User> users;
        std::vector<int> waiting_time;
} pool;

// producer
class MatchHandler: virtual public MatchIf {
    public:
        MatchHandler() {
            // Your initialization goes here
        }

        int32_t add_user(const User& user, const std::string& info) {
            // Your implementation goes here
            printf("add_user\n");
            std::unique_lock<std::mutex> lck(message_queue.m);

            message_queue.q.push({ user, "add" });
            message_queue.cv.notify_all();

            return 0;
        }

        int32_t remove_user(const User& user, const std::string& info) {
            // Your implementation goes here
            printf("remove_user\n");
            std::unique_lock<std::mutex> lck(message_queue.m);

            message_queue.q.push({ user, "remove" });
            message_queue.cv.notify_all();

            return 0;
        }
};

// consumer
void consume_task() {
    while (1) {
        std::unique_lock<std::mutex> lck(message_queue.m);
        if (message_queue.q.empty()) {
            lck.unlock();
            pool.match();
            sleep(1); // match every second
        }
        else {
            auto task = message_queue.q.front();
            message_queue.q.pop();
            lck.unlock(); // unlock timely so that shared varaiable can be released and other task can be continue.

            // do task: add to the match pools
            if (task.operation_type == "add") pool.add(task.user);
            else if (task.operation_type == "remove") pool.remove(task.user);
        }
    }
}

class MatchCloneFactory: virtual public MatchIfFactory {
    public:
        ~MatchCloneFactory() override = default;
        MatchIf* getHandler(const ::apache::thrift::TConnectionInfo& connInfo) override {
            std::shared_ptr<TSocket> sock = std::dynamic_pointer_cast<TSocket>(connInfo.transport);
            std::cout << "Incoming connection\n";
            std::cout << "\tSocketInfo: " << sock->getSocketInfo() << "\n";
            std::cout << "\tPeerHost: " << sock->getPeerHost() << "\n";
            std::cout << "\tPeerAddress: " << sock->getPeerAddress() << "\n";
            std::cout << "\tPeerPort: " << sock->getPeerPort() << "\n";
            return new MatchHandler;
        }
        void releaseHandler(MatchIf* handler) override {
            delete handler;
        }
};

int main(int argc, char** argv) {
    TThreadedServer server(
            std::make_shared<MatchProcessorFactory>(std::make_shared<MatchCloneFactory>()),
            std::make_shared<TServerSocket>(9090), // port
            std::make_shared<TBufferedTransportFactory>(),
            std::make_shared<TBinaryProtocolFactory>());

    std::cout << "Start Match Server\n";

    std::thread matching_thread(consume_task);

    server.serve();
    return 0;
}
