#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <chrono>

//http://localhost:8080/index.html

class ThreadPool {
public:
    void start(size_t numThreads) {
        for (size_t i = 0; i < numThreads; i++) {
            workers.emplace_back([this] {
                while (true) {
                    int client_socket;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { return !tasks.empty() || stop_flag; });
                        if (stop_flag && tasks.empty()) return;
                        client_socket = tasks.front();
                        tasks.pop();
                    }
                    handleRequest(client_socket);
                }
            });
        }
    }

    void addTask(int client_socket) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(client_socket);
        }
        condition.notify_one();
    }

    void stop() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop_flag = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers) {
            if (worker.joinable()) worker.join();
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<int> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop_flag = false;

    static void sendResponse(int client, const std::string& status, const std::string& content) {
        std::ostringstream response;
        response << "HTTP/1.1 " << status << "\r\n";
        response << "Content-Length: " << content.size() << "\r\n"; 
        response << "Content-Type: text/html\r\n\r\n" << content;
        send(client, response.str().c_str(), response.str().length(), 0);
    }
    
    static void handleRequest(int clientSocket) {
    char buffer[4096];
    int received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        close(clientSocket);
        return;
    }
    buffer[received] = '\0';

    std::istringstream iss(buffer);
    std::string method, path;
    iss >> method >> path;

    if (method != "GET") {
        sendResponse(clientSocket, "405 Method Not Allowed", "Method is not 'GET'");
        return;
    }

    if (path == "/") path = "/index.html";

    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file("." + path, std::ios::binary);
    std::string content;
    std::string status = "200 OK";

    if (!file.is_open()) {
        status = "404 Not Found";
        content = "<html><body><h1>404 Not Found</h1></body></html>";
    } else {
        std::ostringstream ss;
        ss << file.rdbuf();
        content = ss.str();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;

    std::cout << "[Log] Path: " << path << " | Time: " << elapsed.count() << " ms" << std::endl;

    sendResponse(clientSocket, status, content);
    close(clientSocket);
}
};

int main() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    if (bind(serverSocket, (sockaddr*)&addr, sizeof(addr)) < 0) return 1;
    listen(serverSocket, 100);

    std::cout << "Server listening on port 8080..." << std::endl;

    ThreadPool pool;
    pool.start(12);

    while (true) {
        int clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket >= 0) {
            pool.addTask(clientSocket);
        }
    }
    pool.stop();
    return 0;
}
