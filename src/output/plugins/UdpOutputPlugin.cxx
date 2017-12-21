/*
 * Copyright 2003-2017 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include "UdpOutputPlugin.hxx"
#include "../OutputAPI.hxx"
#include "Log.hxx"
#include "../Wrapper.hxx"
#include "util/Domain.hxx"

#include <sys/types.h>
#include <string.h>

#ifdef WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif
#include <string>
#include <stdexcept>
#include <unistd.h>
#include <stdio.h>

static constexpr Domain udp_output_domain("udp_output");

class UdpOutput {
  friend struct AudioOutputWrapper<UdpOutput>;

  AudioOutput base;

  int socket;
  sockaddr_in endpoint;

  UdpOutput(const ConfigBlock &block);

public:
  static UdpOutput *Create(EventLoop &event_loop,
                            const ConfigBlock &block);

  void Open(AudioFormat &audio_format);

  void Close() {
    FormatInfo(udp_output_domain, "Closing output.");
    ::close(socket);
    socket = -1;
  }

  size_t Play(const void *chunk, size_t size);
};

UdpOutput::UdpOutput(const ConfigBlock &block)
  : base(udp_output_plugin, block)
{
  auto port = (unsigned short)atoi(block.GetBlockValue("port", ""));
  if (port <= 0)
    throw std::runtime_error("No \"port\" parameter specified");

  // fill in the server's address and data.
  memset((char* )&endpoint, 0, sizeof(endpoint));
  endpoint.sin_family = AF_INET;
  endpoint.sin_port = htons(port);

  // look up the address of the server given its name.
  hostent* hp = gethostbyname("127.0.0.1");
  if (!hp) {
    throw std::runtime_error("Could not obtain address of 127.0.0.1. What a sick joke.");
  }

  // Put the host's address into the server address structure.
  memcpy((void *)&endpoint.sin_addr, hp->h_addr_list[0], hp->h_length);
}

inline UdpOutput *
UdpOutput::Create(EventLoop &, const ConfigBlock &block)
{
  return new UdpOutput(block);
}

inline void
UdpOutput::Open(gcc_unused AudioFormat &audio_format)
{
  if ((socket = ::socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    throw std::runtime_error("cannot create socket");
  }

  FormatInfo(udp_output_domain, "Opened UDP output.");
}

inline size_t
UdpOutput::Play(const void *chunk, size_t size)
{
  if (sendto(socket, (const char* )chunk, size, 0, (struct sockaddr *)&endpoint, sizeof(endpoint)) < 0) {
    perror("sendto failed");
    return 0;
  }

  return size;
}

typedef AudioOutputWrapper<UdpOutput> Wrapper;

const struct AudioOutputPlugin udp_output_plugin = {
  "udp",
  nullptr,
  &Wrapper::Init,
  &Wrapper::Finish,
  nullptr,
  nullptr,
  &Wrapper::Open,
  &Wrapper::Close,
  nullptr,
  nullptr,
  &Wrapper::Play,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
};
