//
//  UDPServer.cpp
//  mpd
//
//  Created by Ryan Walklin on 6/11/17.
//  Copyright © 2017 Ryan Walklin. All rights reserved.
//

#include "UDPServer.hxx"

#include "config.h"
#include "Log.hxx"
#include "config/Block.hxx"
#include "util/Domain.hxx"


#ifdef WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif
#include <string>
#include <string.h>
#include <stdexcept>
#include <unistd.h>
#include <stdio.h>

static constexpr Domain udp_server_domain("udp_server");


UDPServer::UDPServer(const ConfigBlock &block)
                    : socket(0)
                    , endpoint(0)
{
    auto port = (unsigned short)atoi(block.GetBlockValue("udp_port", ""));
    if (port <= 0) {
        throw std::runtime_error("No \"port\" parameter specified");
    }
    endpoint = new struct sockaddr_in;
    // fill in the server's address and data.
    memset((char* )endpoint, 0, sizeof(endpoint));
    endpoint->sin_family = AF_INET;
    endpoint->sin_port = htons(port);
    
    // look up the address of the server given its name.
    auto hostname = block.GetBlockValue("udp_bind_address", "");
    hostent* hp = gethostbyname("127.0.0.1");
    if (!hp) {
        throw std::runtime_error("Could not obtain address from udp_bind_address");
    }
    
    // Put the host's address into the server address structure.
    memcpy((void *)&endpoint->sin_addr, hp->h_addr_list[0], hp->h_length);
    FormatInfo(udp_server_domain, "Created UDP server on %s:%i", hostname, port);

    if ((socket = ::socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        throw std::runtime_error("cannot create socket");
    }
    FormatInfo(udp_server_domain, "Started UDP server.");
}

UDPServer::~UDPServer() {
    FormatInfo(udp_server_domain, "Closing output.");
    ::close(socket);
    socket = -1;
}

size_t UDPServer::Write(const void *data, size_t size) {
    if (sendto(socket, (const char* )data, size, 0, (struct sockaddr *)endpoint, sizeof(*endpoint)) < 0) {
        perror("sendto failed");
        return 0;
    }
    return size;
}



