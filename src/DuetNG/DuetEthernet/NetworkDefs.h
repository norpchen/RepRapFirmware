/*
 * NetworkCommon.h
 *
 *  Created on: 25 Dec 2016
 *      Author: David
 */

#ifndef SRC_DUETNG_DUETETHERNET_NETWORKDEFS_H_
#define SRC_DUETNG_DUETETHERNET_NETWORKDEFS_H_

class NetworkTransaction;
class Socket;
class NetworkBuffer;

// Definition of how a Connection is represented, for Webserver module
typedef Socket *Connection;
const Connection NoConnection = nullptr;

typedef uint8_t SocketNumber;
const SocketNumber NoSocket = 255;

typedef uint16_t Port;

const uint8_t DefaultMacAddress[6] = { 0xBE, 0xEF, 0xDE, 0xAD, 0xFE, 0xED };	// Need some sort of default...
const uint8_t DefaultIpAddress[4] = { 0, 0, 0, 0 };				// Need some sort of default...
const uint8_t DefaultNetMask[4] = { 255, 255, 255, 0 };
const uint8_t DefaultGateway[4] = { 0, 0, 0, 0 };

const Port DefaultHttpPort = 80;
const Port FTP_PORT = 21;
const Port TELNET_PORT = 23;

const unsigned int TCP_MSS = 1460;

#endif /* SRC_DUETNG_DUETETHERNET_NETWORKDEFS_H_ */
