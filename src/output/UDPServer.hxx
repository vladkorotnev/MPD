//
//  UDPServer.hpp
//  mpd
//
//  Created by Ryan Walklin on 6/11/17.
//  Copyright © 2017 Ryan Walklin. All rights reserved.
//

#ifndef UDPServer_hxx
#define UDPServer_hxx

#include <stdio.h>

struct ConfigBlock;
struct sockaddr_in;

class UDPServer {
    
private:
    int socket;
    sockaddr_in *endpoint;
    
public:
    UDPServer(const ConfigBlock &block);
    ~UDPServer();
    
    size_t Write(const void *data, size_t size);
};

#endif /* UDPServer_hxx */

