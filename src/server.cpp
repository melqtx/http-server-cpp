#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <netdb.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

struct HttpRequest {
  std::string method;
  std::string uri;
  std::string version;
  std::map<std::string, std::string> headers;
  std::string body;
};

std::string trim(std::string str) {
  auto start = str.begin();
  while (start != str.end() && std::isspace(*start)) {
    start++;
  }
  auto end = str.end();
  do {
    end--;
  } while (std::distance(start, end) > 0 && std::isspace(*end));
  std::string res = std::string(start, end + 1);
  return res;
}
// Function to parse the HTTP request
HttpRequest parseHttpRequest(std::string request) {
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
      std::string headerValue = trim(
          line.substr(colon + 1)); // Skip ":" (no space to accommodate trim)
      httpRequest.headers[headerName] = headerValue;
    }
  }
  // Parse body (if any)
  std::string body;
  while (std::getline(stream, line)) {
    if (!line.empty()) {
      body += line + "\n";
    }
  }
  httpRequest.body = trim(body);
  return httpRequest;
}
std::string getResponse(std::string &client_req) {
  HttpRequest httpRequest = parseHttpRequest(client_req);
  std::string response;
  if (httpRequest.method == "GET" && httpRequest.uri == "/") {
    response = "HTTP/1.1 200 OK\r\n\r\n";
  } else if (httpRequest.method == "GET" &&
             httpRequest.uri.find("/echo/") == 0) {
    std::string resBody = httpRequest.uri.substr(6);
    response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
    response += "Content-Length: " + std::to_string(resBody.length());
    response += "\r\n\r\n";
    response += resBody;
  } else if (httpRequest.method == "GET" && httpRequest.uri == "/user-agent") {
    std::string resBody = httpRequest.headers["User-Agent"];
    response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
    response += "Content-Length: " + std::to_string(resBody.length());
    response += "\r\n\r\n";
    response += resBody;
  } else if (httpRequest.method == "GET" &&
             httpRequest.uri.find("/files") == 0) {
    std::string filePath = httpRequest.uri.substr(7);
    std::string basePath = "/tmp/data/codecrafters.io/http-server-tester/";
    filePath = basePath + filePath;
    std::ifstream file(filePath);
    std::cout << "filepath: " << filePath << "\n";
    if (!file.is_open()) {
      std::cerr << "Failed to open file: " << filePath << std::endl;
      response = "HTTP/1.1 404 Not Found\r\n\r\n";
      return response;
    }
    std::string content;
    std::string line;
    while (std::getline(file, line)) {
      content += line;
    }
    file.close();
    std::cout << "file content " << content << "\n";
    std::filesystem::path fsPath = filePath;
    int filesize = std::filesystem::file_size(filePath);
    std::cout << "file size " << filesize << "\n";
    response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n";
    response += "Content-Length: " + std::to_string(filesize);
    response += "\r\n\r\n";
    response += content;
  } else {
    response = "HTTP/1.1 404 Not Found\r\n\r\n";
  }
  return response;
}
int createServerSocket() {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "Failed to create server socket\n";
    exit(EXIT_FAILURE);
  }
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    std::cerr << "setsockopt failed\n";
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  int PORT = 4221;
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);
  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) !=
      0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
    std::cerr << "Failed to bind to port " << PORT << "\n";
    close(server_fd);
    exit(EXIT_FAILURE);
  }
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    close(server_fd);
    exit(EXIT_FAILURE);
  }
  return server_fd;
}
void acceptNewClient(int server_fd, std::vector<int> &client_sockets) {
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  int new_client =
      accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
  if (new_client < 0) {
    std::cerr << "new client error\n";
    return;
  }
  std::cout << "Client connected\n";
  for (int &socket : client_sockets) {
    if (socket == 0) {
      socket = new_client;
      break;
    }
  }
}
void handleClient(int client_fd) {
  int BUFFER_SIZE = 1024;
  std::string client_req(BUFFER_SIZE, '\0');
  ssize_t bytesReceived = recv(client_fd, &client_req[0], BUFFER_SIZE, 0);
  if (bytesReceived < 0) {
    std::cerr << "Failed to receive data." << std::endl;
    close(client_fd);
    return;
  }
  std::string response = getResponse(client_req);
  send(client_fd, response.c_str(), response.length(), 0);
  close(client_fd);
}
int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  // You can use print statements as follows for debugging, they'll be visible
  // when running tests.
  std::cout << "Logs from your program will appear here!\n";
  int server_fd = createServerSocket();
  int MAX_CLIENTS = 30;
  std::vector<int> client_sockets(MAX_CLIENTS, 0);

  fd_set clientfds;

  int max_sd = server_fd;
  while (true) {
    FD_ZERO(&clientfds);
    FD_SET(server_fd, &clientfds);
    int max_sd = server_fd;
    for (int sd : client_sockets) {
      if (sd > 0)
        FD_SET(sd, &clientfds);
      // highest file descriptor number, need it for the select function
      if (sd > max_sd)
        max_sd = sd;
    }
    int activity = select(max_sd + 1, &clientfds, NULL, NULL, NULL);
    if ((activity < 0) && (errno != EINTR)) {
      std::cerr << "select error\n";
    }
    // new client
    if (FD_ISSET(server_fd, &clientfds)) {
      acceptNewClient(server_fd, client_sockets);
    }
    // client operations
    for (int &curr_fd : client_sockets) {
      if (FD_ISSET(curr_fd, &clientfds)) {
        handleClient(curr_fd);
        curr_fd = 0;
      }
    }
  }
  // int client_fd = accept(server_fd, (struct sockaddr*)&client_addr,
  //                        (socklen_t*)&client_addr_len);
  // std::cout << "Client connected\n";
  // // Receive data from the client
  // std::string client_req(1024, '\0');
  // ssize_t bytesReceived = recv(client_fd, &client_req[0], 1024, 0);
  // if (bytesReceived == -1) {
  //     std::cerr << "Failed to receive data." << std::endl;
  //     close(client_fd);
  //     close(server_fd);
  //     return 1;
  // }
  // std::string response = getResponse(client_req);
  // send(client_fd, response.c_str(), response.length(), 0);
  close(server_fd);
  return 0;
}