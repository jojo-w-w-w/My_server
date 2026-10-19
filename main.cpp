#include <iostream>
#include <cstdio>
#include <string>

#include <fstream>
#include <sstream>

#include <sys/time.h>
#include <cerrno>

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>

#include <poll.h>
#include <unistd.h>  // STDIN_FILENO

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

bool parseRequestLine(const std::string& request, std::string& method, std::string& path, std::string& version)
{
    //截出第一行
    auto lineEnd = request.find("\r\n");

    if(lineEnd == std::string::npos)
    {
        return false;
    }

     std::string requestLine = request.substr(0, lineEnd);

    auto FirstSpacePos = requestLine.find(" ");     // 只在第一行找第一个空格

    if(FirstSpacePos == std::string::npos || FirstSpacePos == 0)
    {
        return false;
    }
    else//第一个空格合法，继续找第二个空格
    {
        auto SecondSpacePos = requestLine.find(' ', FirstSpacePos + 1);   // 从 start 位置开始找下一个空格
        if(SecondSpacePos == std::string::npos)
        {
            return false;
        }
        else if(SecondSpacePos == FirstSpacePos + 1 || SecondSpacePos + 1 == requestLine.size() || requestLine.find(' ', SecondSpacePos + 1) != std::string::npos)
        {//路径为空， 版本为空， 第二个空格后面还有空格
            return false;
        }
        else
        {
            method = requestLine.substr
                            (
                                0,
                                FirstSpacePos
                            );

            path = requestLine.substr
                                    (
                                        FirstSpacePos + 1,
                                        SecondSpacePos - FirstSpacePos - 1
                                    );

            version = requestLine.substr
                                    (
                                        SecondSpacePos + 1
                                    );
        }
    }
    
    return true;
}

std::string buildResponse(const std::string& status, const std::string& body, const std::string& contentType, bool includebody)
{
    // 构造发送给客户端的 HTTP 响应
    std::string response =
    "HTTP/1.1 "+ status + "\r\n"                                //响应状态行
    "Content-Type: " + contentType + "\r\n"                     //正文是 UTF-8 编码的纯文本
    "Content-Length: " + std::to_string(body.size()) + "\r\n"   //正文的实际字节数
    "Connection: close\r\n"                                     //本次响应后关闭连接
    "\r\n";                                                     //空行，分隔响应头和正文 
    
    if(includebody == true)
    {
        response += body;                                       //客户端最终看到的正文
    }

    return response;
}

bool readFile(const std::string& filePath, std::string& content)
{
    //指令，路径正确打开文件
    std::ifstream file(filePath, std::ios::binary);
    if(file.is_open())
    {
        //如果文件正常打开
        std::ostringstream contents;
        contents << file.rdbuf();
        content = contents.str();
        return true;
    }
    return false;
}

void sendAll(int clientFd, const std::string& response)
{
    // 已经发送了多少字节
    size_t totalSent = 0;

    while (totalSent < response.size())
    {
        //发送数据
        ssize_t sent = send
        (
            clientFd,
            response.data() + totalSent,
            response.size() - totalSent,
            MSG_NOSIGNAL                            //MSG_NOSIGNAL 用于避免向已断开的连接发送时，程序因 SIGPIPE 信号直接终止
        );

        if (sent == -1)
        {
            perror("send");
            break;
        }

        if (sent == 0)
        {
            std::cerr << "发送未取得进展，停止发送。" << std::endl;
            break;
        }

        totalSent += static_cast<size_t>(sent);
    }
}

void handleClient(int clientFd)
{
    std::cout << "客户端连接成功！" << std::endl;

    std::string request;

    bool headersComplete = false;

    // 准备一块内存，用来存放接收到的数据
    char buffer[4096]{};

    timeval timeout{};
    timeout.tv_sec = 3;   // 秒
    timeout.tv_usec = 0;  // 微秒

    int result = setsockopt
    (
        clientFd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    );

    if(result == -1)
    {
        perror("setsockopt");
        close(clientFd);
        return;
    }

    //接收循环
    while(headersComplete == false)
    {
        // 从这个客户端连接接收数据
        ssize_t received = recv(clientFd, buffer, sizeof(buffer), 0);
        
        // 有接收数据并且“请求头”不完整
        if(received > 0 && headersComplete == false)
        {
            std::cout << "本次 recv 收到" << received << " 字节" << std::endl;

            // 请求头长度检测
            if (request.size() + static_cast<size_t>(received) > 8192)
            {
                std::cerr << "客户端请求头长度超过限制\n";
                std::string errorResponse = buildResponse(
                    "431 Request Header Fields Too Large",
                    "Request Header Fields Too Large\n",
                    "text/plain; charsetr=utf-8",
                    true
                );
                sendAll(clientFd, errorResponse);
                break;
            }

            request.append(buffer, received);
            if(request.find("\r\n\r\n") != std::string::npos)
            {
                headersComplete = true;
            }
        }
        else if (received == 0)
        {
            std::cout << "客户端已结束发送。" << std::endl;
            break;
        }
        else if(received == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                std::cout << "接收超时。" << std::endl;
            }
            else
            {
                perror("recv");
            }
            
            break;
        }
    }

    if(headersComplete == true)
    {
        //请求行是否有效标志
        std::string method;
        std::string path;
        std::string version;

        bool requestLineValid = parseRequestLine(request, method, path, version);

        if(requestLineValid)
        {
            std::cout << "提取方法为: " << method << std::endl;
            std::cout << "请求路径为: " << path << std::endl;
            std::cout << "版本为: " << version << std::endl;
        }

        //设置响应状态和正文主体
        std::string status;
        std::string body;
        std::string contentType = "text/plain; charset=utf-8";

        bool includebody = !(requestLineValid && method == "HEAD");

        std::string filePath;

        if(!requestLineValid)
        {   
            status = "400 Bad Request";
            body = "Bad Request\n";
        }
        else if(version != "HTTP/1.1")//版本检查
        {
            status = "505 HTTP Version Not Supported";
            body = "HTTP Version Not Supported\n";
        }
        else if(method != "GET" && method != "HEAD")//请求头检查
        {
            status = "501 Not Implemented";
            body = "Wrong Request\n";
        }
        else
        {  
            if(path == "/" || path == "/index.html")
            {
                filePath = "www/index.html";
            }
            else if(path == "/about.html")
            {
                filePath = "www/about.html";
            }
            else if(path == "/hello")
            {
                status = "200 OK";
                body = "Hello\n";
            }
            else
            {
                status = "404 Not Found";
                body = "Not Found\n";
            }

            if(!filePath.empty())
            {
                if(readFile(filePath, body))
                {//如果文件正常打开
                    status = "200 OK";
                    contentType = "text/html; charset=utf-8";
                }
                else
                {
                    status = "500 Internal Server Error";
                    body = "Internal Server Error\n";
                }
            } 
        }
        
        // 构造发送给客户端的 HTTP 响应
        std::string response = buildResponse(status, body, contentType, includebody);
        
        sendAll(clientFd, response);
    }

    //当前客户端处理完毕
    close(clientFd);
}

int main()
{
    // 创建一个用于 IPv4、TCP 通信的 socket
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);

    if (listenFd == -1)
    {
        perror("socket");
        return 1;
    }

    // 设置服务器使用的 IP 地址和端口
    sockaddr_in address{};//sockaddr_in 是系统定义的 IPv4 地址结构体类型
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);//host to network short, 将 16 位端口号转换成网络字节序
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);//host to network long, 将 32 位 IPv4 地址转换成网络字节序
    //因为多字节整数在不同机器上的存储顺序可能不同(大小端), 所以需要转换

    // 地址复用
    int reuse = 1;//reuse = 1表示开启
    if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR,//SO_REUSEADDR允许符合条件的地址重新绑定
                &reuse, sizeof(reuse)) == -1)
    {           //&reuse 把这个值的地址传给系统
        perror("setsockopt SO_REUSEADDR");
        close(listenFd);
        return 1;
    }

    // 将 socket 绑定到 127.0.0.1:8080
    if( bind
        (
            listenFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)
        ) == -1 )
    {
        perror("bind");
        close(listenFd);
        return 1;
    }

    // 4. 开始监听连接请求
    if (listen(listenFd, 5) == -1)
    {//第二个参数叫 backlog。在 Linux 的 TCP 服务中，它指定的是：已完成连接建立、但还没被程序通过 accept() 取走的连接队列的长度上限，实际还受系统上限约束。
        perror("listen");
        close(listenFd);
        return 1;
    }

    std::cout << "服务器启动，正在等待客户端连接……" << std::endl;

    //多线程
    std::queue<int> clientQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondition;

    std::vector<std::thread> workers;

    bool stopping = false;

    for(int i = 0; i < 4; ++i)
    {
        workers.emplace_back
        ([&]
        {
            while(true)
            {
                int clientFd;

                {
                    std::unique_lock<std::mutex> lock(queueMutex); // 加锁

                    queueCondition.wait(lock, [&] {
                        return stopping || !clientQueue.empty();
                    }); // 队列为空就等待；等待期间会释放锁

                    if(stopping && clientQueue.empty())
                    {
                        return;
                    }

                    clientFd = clientQueue.front(); // 取得队头编号
                    clientQueue.pop();              // 从队列删除
                } // 自动解锁

                handleClient(clientFd);
            }
        });
    }

    bool acceptFailed = false;

    pollfd watched[2] = {
        {listenFd, POLLIN, 0},     // [0]：有新连接可接受
        {STDIN_FILENO, POLLIN, 0}  // [1]：终端有输入可读取
    };

    std::cout << "输入 q 并按回车，可停止服务器。\n";
    
    // 5. 等待并接受多个客户端连接
    
    while(true)
    {
        int ready = poll(watched, 2, -1);

        if (ready == -1)
        {
            if (errno == EINTR) continue; // 被信号打断，重新等待
            perror("poll");
            acceptFailed = true;
            break;
        }

        // 先处理停止命令
        if (watched[1].revents & POLLIN)
        {
            std::string command;
            if (!std::getline(std::cin, command) || command == "q")
            {
                break;
            }
        }

        // 有新连接时，才执行 accept
        if (watched[0].revents & POLLIN)
        {
            int clientFd = accept(listenFd, nullptr, nullptr);
            
            if (clientFd == -1)
            {
                perror("accept");
                acceptFailed = true;
                break;
            }

            //普通函数调用，主线程必须等它执行完，才能继续下一次 accept。
            //handleClient(clientFd);

            bool queued = false;

            //改用多线程
            {
                std::lock_guard<std::mutex> lock(queueMutex); // 加锁

                if(clientQueue.size() < 16)
                {
                    clientQueue.push(clientFd);               // 入队
                    queued = true;
                }
                
            } // 离开花括号，自动解锁
            
            if(queued == true)//
            {
                queueCondition.notify_one(); // 通知一个等待的工作线程
            }
            else
            {
                std::string queuedResponse = buildResponse(
                    "503 Service Unavailable",
                    "Service Unavailable\n",
                    "text/plain; charsetr=utf-8",
                    true
                );
                sendAll(clientFd, queuedResponse);
                close(clientFd);
            }
        }
    }

    close(listenFd);

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopping = true;
    }

    queueCondition.notify_all();

    for (std::thread& worker : workers)
    {
        worker.join();
    }

    return acceptFailed ? 1 : 0;

}