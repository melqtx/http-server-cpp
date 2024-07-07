// Include necessary headers
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Constants
const int PORT = 4221;
const int MAX_CLIENTS = 30;
const int BUFFER_SIZE = 1024;
const int CONNECTION_BACKLOG = 5;

// Global variables
std::string basePath;

// HTTP request structure
struct HttpRequest {
  std::string method;
  std::string uri;
  std::string version;
  std::map<std::string, std::string> headers;
  std::string body;
};

// Function to trim whitespace from both ends of a string
std::string trim(const std::string &str) {
  auto start = std::find_if_not(
      str.begin(), str.end(), [](unsigned char c) { return std::isspace(c); });
  auto end = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char c) {
               return std::isspace(c);
             }).base();
  return (start < end) ? std::string(start, end) : std::string();
}

// Function to parse the HTTP request
HttpRequest parseHttpRequest(const std::string &request) {
  HttpRequest httpRequest;
  std::istringstream stream(request);
  std::string line;

  // Parse request line
  if (std::getline(stream, line)) {
    std::istringstream requestLineStream(line);
    requestLineStream >> httpRequest.method >> httpRequest.uri >>
        httpRequest.version;
    httpRequest.method = trim(httpRequest.method);
    httpRequest.uri = trim(httpRequest.uri);
    httpRequest.version = trim(httpRequest.version);
  }

  // Parse headers
  while (std::getline(stream, line) && line != "\r") {
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string headerName = trim(line.substr(0, colon));
      std::string headerValue = trim(line.substr(colon + 1));
      httpRequest.headers[headerName] = headerValue;
    }
  }

  // Parse body if Content-Length is present
  auto contentLengthHeader = httpRequest.headers.find("Content-Length");
  if (contentLengthHeader != httpRequest.headers.end()) {
    int contentLength = std::stoi(contentLengthHeader->second);
    httpRequest.body.resize(contentLength);
    stream.read(&httpRequest.body[0], contentLength);
  }

  return httpRequest;
}

// Function to compress a string using zlib
std::string compressString(const std::string &str,
                           int compressionLevel = Z_BEST_COMPRESSION) {
  z_stream zs;
  memset(&zs, 0, sizeof(zs));

  if (deflateInit2(&zs, compressionLevel, Z_DEFLATED, 31, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    throw std::runtime_error("deflateInit failed while compressing.");
  }

  zs.next_in = (Bytef *)str.data();
  zs.avail_in = str.size();

  int ret;
  char outbuffer[32768];
  std::string outstring;

  // Compress the input string
  do {
    zs.next_out = reinterpret_cast<Bytef *>(outbuffer);
    zs.avail_out = sizeof(outbuffer);
    ret = deflate(&zs, Z_FINISH);
    if (outstring.size() < zs.total_out) {
      outstring.append(outbuffer, zs.total_out - outstring.size());
    }
  } while (ret == Z_OK);

  deflateEnd(&zs);

  if (ret != Z_STREAM_END) {
    throw std::runtime_error("Exception during zlib compression: " +
                             std::to_string(ret));
  }

  return outstring;
}

// Function to handle different types of HTTP requests
std::string handleHttpRequest(const HttpRequest &httpRequest) {
  std::string response;

  if (httpRequest.method == "GET" && httpRequest.uri == "/") {
    response = "HTTP/1.1 200 OK\r\n\r\n";
  } else if (httpRequest.method == "GET" &&
             httpRequest.uri.find("/echo/") == 0) {
    std::string resBody = httpRequest.uri.substr(6);
    response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";

    bool compressed = false;
    auto it = httpRequest.headers.find("Accept-Encoding");
    if (it != httpRequest.headers.end()) {
      std::istringstream contentEncStream(it->second);
      std::string enc;
      while (std::getline(contentEncStream, enc, ',')) {
        if (trim(enc) == "gzip") {
          response += "Content-Encoding: gzip\r\n";
          compressed = true;
          break;
        }
      }
    }

    if (compressed) {
      std::string compBody = compressString(resBody);
      response += "Content-Length: " + std::to_string(compBody.length()) +
                  "\r\n\r\n" + compBody;
    } else {
      response += "Content-Length: " + std::to_string(resBody.length()) +
                  "\r\n\r\n" + resBody;
    }
  } else if (httpRequest.method == "GET" && httpRequest.uri == "/user-agent") {
    std::string resBody = httpRequest.headers["User-Agent"];
    response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
    response += "Content-Length: " + std::to_string(resBody.length()) +
                "\r\n\r\n" + resBody;
  } else if (httpRequest.method == "GET" &&
             httpRequest.uri.find("/files/") == 0) {
    std::string filePath = basePath + httpRequest.uri.substr(7);
    std::ifstream file(filePath, std::ios::binary);

    if (!file) {
      response = "HTTP/1.1 404 Not Found\r\n\r\n";
    } else {
      std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
      file.close();

      response =
          "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n";
      response += "Content-Length: " + std::to_string(content.length()) +
                  "\r\n\r\n" + content;
    }
  } else if (httpRequest.method == "POST" &&
             httpRequest.uri.find("/files/") == 0) {
    std::string filePath = basePath + httpRequest.uri.substr(7);
    std::ofstream file(filePath, std::ios::binary);

    if (file) {
      file << httpRequest.body;
      file.close();
      response = "HTTP/1.1 201 Created\r\n\r\n";
    } else {
      response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
    }
  } else {
    response = "HTTP/1.1 404 Not Found\r\n\r\n";
  }

  return response;
}

// Function to create and set up the server socket
int createServerSocket() {
  int serverFd = socket(AF_INET, SOCK_STREAM, 0);
  if (serverFd < 0) {
    throw std::runtime_error("Failed to create server socket");
  }

  int reuse = 1;
  if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    close(serverFd);
    throw std::runtime_error("setsockopt failed");
  }

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = INADDR_ANY;
  serverAddr.sin_port = htons(PORT);

  if (bind(serverFd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != 0) {
    close(serverFd);
    throw std::runtime_error("Failed to bind to port " + std::to_string(PORT));
  }

  if (listen(serverFd, CONNECTION_BACKLOG) != 0) {
    close(serverFd);
    throw std::runtime_error("listen failed");
  }

  return serverFd;
}

// Function to accept a new client connection
int acceptNewClient(int serverFd) {
  struct sockaddr_in clientAddr;
  socklen_t clientAddrLen = sizeof(clientAddr);
  int newClient =
      accept(serverFd, (struct sockaddr *)&clientAddr, &clientAddrLen);
  if (newClient < 0) {
    throw std::runtime_error("Failed to accept new client");
  }
  std::cout << "Client connected\n";
  return newClient;
}

// Function to handle client request
void handleClient(int clientFd) {
  std::vector<char> buffer(BUFFER_SIZE);
  ssize_t bytesReceived = recv(clientFd, buffer.data(), buffer.size(), 0);
  if (bytesReceived < 0) {
    std::cerr << "Failed to receive data." << std::endl;
    close(clientFd);
    return;
  }

  std::string clientReq(buffer.data(), bytesReceived);
  HttpRequest httpRequest = parseHttpRequest(clientReq);
  std::string response = handleHttpRequest(httpRequest);

  send(clientFd, response.c_str(), response.length(), 0);
  close(clientFd);
}

int main(int argc, char **argv) {
  // Enable unbuffered output
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  std::cout << "Server starting...\n";

  // Parse command-line arguments
  if (argc == 3 && strcmp(argv[1], "--directory") == 0) {
    basePath = argv[2];
  }

  try {
    int serverFd = createServerSocket();
    std::cout << "Server listening on port " << PORT << std::endl;

    std::vector<int> clientSockets(MAX_CLIENTS, 0);
    fd_set clientfds;

    while (true) {
      FD_ZERO(&clientfds);
      FD_SET(serverFd, &clientfds);

      int maxSd = serverFd;
      for (int sd : clientSockets) {
        if (sd > 0) {
          FD_SET(sd, &clientfds);
          maxSd = std::max(maxSd, sd);
        }
      }

      int activity = select(maxSd + 1, &clientfds, NULL, NULL, NULL);
      if ((activity < 0) && (errno != EINTR)) {
        throw std::runtime_error("select error");
      }

      if (FD_ISSET(serverFd, &clientfds)) {
        int newClient = acceptNewClient(serverFd);
        auto it = std::find(clientSockets.begin(), clientSockets.end(), 0);
        if (it != clientSockets.end()) {
          *it = newClient;
        } else {
          throw std::runtime_error("Max clients reached");
        }
      }

      for (int &currFd : clientSockets) {
        if (currFd > 0 && FD_ISSET(currFd, &clientfds)) {
          handleClient(currFd);
          currFd = 0;
        }
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
