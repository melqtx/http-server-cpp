#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // You can use print statements as follows for debugging, they'll be visible
  // when running tests.
  std::cout << "Logs from your program will appear here!\n";

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  // // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) !=
      0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  std::cout << "Waiting for a client to connect...\n";
  // Task new

  // Accept the incoming client connection
  int client = accept(server_fd, (struct sockaddr *)&client_addr,
                      (socklen_t *)&client_addr_len);
  if (client < 0) {
    std::cerr << "Failed to accept client connection\n";
    close(server_fd);
    return 1;
  }

  // Create a buffer to store the received request
  char buffer[1024] = {0};

  // Receive the client's HTTP request
  int bytes_received = recv(client, buffer, sizeof(buffer), 0);
  if (bytes_received < 0) {
    std::cerr << "Failed to receive data\n";
    close(client);
    close(server_fd);
    return 1;
  }

  // Convert the received data to a std::string for easier manipulation
  std::string request(buffer);

  // Find the positions of the method and URL in the request
  std::string::size_type method_end = request.find(' ');
  std::string::size_type url_end = request.find(' ', method_end + 1);

  // Extract the URL from the request
  std::string url = request.substr(method_end + 1, url_end - method_end - 1);

  // Prepare the response based on the requested URL
  std::string response;
  if (url == "/") {
    // If the URL is "/", send a simple "Hello, World!" response
    response = "HTTP/1.1 200 OK\r\n\r\nHello, World!";
  } else if (url.substr(0, 6) == "/echo/") {
    // If the URL starts with "/echo/", extract the string after "/echo/"
    std::string echo_str = url.substr(6);

    // Prepare a response with the extracted string
    response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: text/plain\r\n";
    response += "Content-Length: " + std::to_string(echo_str.length()) + "\r\n";
    response += "\r\n" + echo_str;
  } else {
    // For any other URL, send a 404 Not Found response
    response = "HTTP/1.1 404 Not Found\r\n\r\n404 Not Found";
  }

  // Send the prepared response to the client
  send(client, response.c_str(), response.length(), 0);

  // Log the requested URL
  std::cout << "Client connected, URL requested: " << url << "\n";

  // Close the client connection and server socket
  close(client);
  close(server_fd);

  return 0;