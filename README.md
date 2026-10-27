# My_server

一个使用 C++17 和 Linux Socket 编写的 HTTP 服务器学习项目。主线程通过 `poll` 等待新连接和终端输入，将连接交给四个工作线程处理；服务器可以返回文本以及 `www/` 目录中的静态文件。

## 环境与运行

需要 Linux、支持 C++17 的编译器、CMake 3.16 或更新版本。以下命令均在项目根目录执行：

```bash
cmake -S . -B build
cmake --build build
./build/server
```

服务器监听 `127.0.0.1:8080`，只能从本机访问。运行时在服务器终端输入 `q` 并按回车，可以等待工作线程结束后退出。请从项目根目录启动程序，因为静态文件路径相对于当前工作目录。

## 试一试

保持服务器运行，另开一个终端：

```bash
curl -i http://127.0.0.1:8080/
curl -i http://127.0.0.1:8080/hello
curl -i 'http://127.0.0.1:8080/hello?name=JOJO'
curl -I http://127.0.0.1:8080/about.html
```

| 请求 | 结果 |
| --- | --- |
| `GET /` | 返回 `www/index.html` |
| `GET /hello` | 返回 `Hello` |
| `GET /hello?name=JOJO` | 返回 `Hello, JOJO` |
| `GET /about.html` | 返回 `www/about.html` |
| `GET /style.css` | 返回 `www/style.css` |
| `HEAD` 请求 | 返回与对应 GET 请求相同的状态和响应头，不发送响应正文 |

其他位于 `www/` 中的普通文件也可以通过相应路径访问；文件不存在时返回 404。服务器依据 `.html`、`.css` 和 `.png` 后缀设置常见的 `Content-Type`，其他后缀使用 `application/octet-stream`。

## 实现要点与限制

- 仅处理 HTTP/1.1 的 GET 和 HEAD。请求行格式错误返回 400，其他请求方法返回 501，不支持的 HTTP 版本返回 505。
- 每个连接处理一个请求，响应中包含 `Connection: close`，发送完后关闭连接；没有实现长连接、HTTPS、通用查询参数解析或请求体处理。
- 请求头最多 8192 字节；超过限制返回 431。静态文件最多 5 MiB；超过限制返回 403。
- 通过真实路径检查限制静态文件访问范围，避免读取 `www/` 目录外的目标文件。
- 四个工作线程从队列取连接；队列最多等待 16 个连接，满时返回 503。
- 接收和发送分别设置了 3 秒的阻塞调用超时。这是单次 I/O 等待时间限制，不是整个请求的总时限。

这是用于学习 Socket、HTTP 报文、线程同步和文件读取的实现，尚未覆盖完整 HTTP 协议规范。
