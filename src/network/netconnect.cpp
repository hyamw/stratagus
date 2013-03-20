//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ |
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/**@name netconnect.cpp - The network high level connection code. */
//
//      (c) Copyright 2001-2007 by Lutz Sammer, Andreas Arens, and Jimmy Salmon
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
//      02111-1307, USA.
//

//@{

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "stratagus.h"

#include "netconnect.h"

#include "interface.h"
#include "map.h"
#include "master.h"
#include "net_lowlevel.h"
#include "network.h"
#include "parameters.h"
#include "player.h"
#include "script.h"
#include "settings.h"
#include "version.h"
#include "video.h"

//----------------------------------------------------------------------------
// Declaration
//----------------------------------------------------------------------------

#define NetworkDefaultPort 6660  /// Default communication port

// received nothing from client for xx frames?
#define CLIENT_LIVE_BEAT 60
#define CLIENT_IS_DEAD 300

/**
**  Connect state information of network systems active in current game.
*/
struct NetworkState
{
	unsigned char  State;   /// Menu: ConnectState
	unsigned short MsgCnt;  /// Menu: Counter for state msg of same type (detect unreachable)
	// Fill in here...
};

//----------------------------------------------------------------------------
// Variables
//----------------------------------------------------------------------------

char *NetworkAddr = NULL;              /// Local network address to use
int NetworkPort = NetworkDefaultPort;  /// Local network port to use

static unsigned long NetworkServerIP;   /// Network Client: IP of server to join
static int NetworkServerPort = NetworkDefaultPort; /// Server network port to use

int HostsCount;                        /// Number of hosts.
CNetworkHost Hosts[PlayerMax];         /// Host and ports of all players.

int NetConnectRunning;                 /// Network menu: Setup mode active
static NetworkState NetStates[PlayerMax]; /// Network menu: Server: Client Host states
unsigned char NetLocalState;           /// Network menu: Local Server/Client connect state;
int NetLocalHostsSlot;                 /// Network menu: Slot # in Hosts array of local client
int NetLocalPlayerNumber;              /// Player number of local client

static int NetStateMsgCnt;              /// Number of consecutive msgs of same type sent
static unsigned char LastStateMsgType;  /// Subtype of last InitConfig message sent
static unsigned long NetLastPacketSent; /// Tick the last network packet was sent

int NetPlayers;                         /// How many network players
std::string NetworkMapName;             /// Name of the map received with ICMMap
static int NoRandomPlacementMultiplayer = 0; /// Disable the random placement of players in muliplayer mode

CServerSetup ServerSetupState; // Server selection state for Multiplayer clients
CServerSetup LocalSetupState;  // Local selection state for Multiplayer clients


const unsigned char *CNetworkHost::Serialize() const
{
	unsigned char *buf = new unsigned char[CNetworkHost::Size()];
	unsigned char *p = buf;

	*(uint32_t *)p = htonl(this->Host);
	p += 4;
	*(uint16_t *)p = htons(this->Port);
	p += 2;
	*(uint16_t *)p = htons(this->PlyNr);
	p += 2;
	memcpy(p, this->PlyName, sizeof(this->PlyName));

	return buf;
}

void CNetworkHost::Deserialize(const unsigned char *p)
{
	this->Host = ntohl(*(uint32_t *)p);
	p += 4;
	this->Port = ntohs(*(uint16_t *)p);
	p += 2;
	this->PlyNr = ntohs(*(uint16_t *)p);
	p += 2;
	memcpy(this->PlyName, p, sizeof(this->PlyName));
}

void CNetworkHost::Clear()
{
	this->Host = 0;
	this->Port = 0;
	this->PlyNr = 0;
	memset(this->PlyName, 0, sizeof(this->PlyName));
}

void CNetworkHost::SetName(const char *name)
{
	strncpy_s(this->PlyName, sizeof(this->PlyName), name, _TRUNCATE);
}

const unsigned char *CServerSetup::Serialize() const
{
	unsigned char *buf = new unsigned char[CServerSetup::Size()];
	unsigned char *p = buf;

	*p++ = this->ResourcesOption;
	*p++ = this->UnitsOption;
	*p++ = this->FogOfWar;
	*p++ = this->RevealMap;
	*p++ = this->TilesetSelection;
	*p++ = this->GameTypeOption;
	*p++ = this->Difficulty;
	*p++ = this->MapRichness;
	for (int i = 0; i < PlayerMax; ++i) {
		*p++ = this->CompOpt[i];
	}
	for (int i = 0; i < PlayerMax; ++i) {
		*p++ = this->Ready[i];
	}
	for (int i = 0; i < PlayerMax; ++i) {
		*p++ = this->Race[i];
	}
	for (int i = 0; i < PlayerMax; ++i) {
		*(uint32_t *)p = htonl(this->LastFrame[i]);
		p += 4;
	}
	return buf;
}

void CServerSetup::Deserialize(const unsigned char *p)
{
	this->ResourcesOption = *p++;
	this->UnitsOption = *p++;
	this->FogOfWar = *p++;
	this->RevealMap = *p++;
	this->TilesetSelection = *p++;
	this->GameTypeOption = *p++;
	this->Difficulty = *p++;
	this->MapRichness = *p++;
	for (int i = 0; i < PlayerMax; ++i) {
		this->CompOpt[i] = *p++;
	}
	for (int i = 0; i < PlayerMax; ++i) {
		this->Ready[i] = *p++;
	}
	for (int i = 0; i < PlayerMax; ++i) {
		this->Race[i] = *p++;
	}
	for (int i = 0; i < PlayerMax; ++i) {
		this->LastFrame[i] = ntohl(*(Uint32 *)p);
		p += 4;
	}
}

CInitMessage::CInitMessage()
{
	memset(this, 0, sizeof(CInitMessage));

	this->Stratagus = StratagusVersion;
	this->Version = NetworkProtocolVersion;
	this->Lag = NetworkLag;
	this->Updates = NetworkUpdates;
}

const unsigned char *CInitMessage::Serialize() const
{
	unsigned char *buf = new unsigned char[CInitMessage::Size()];
	unsigned char *p = buf;

	*p++ = this->Type;
	*p++ = this->SubType;
	*p++ = this->HostsCount;
	*p++ = this->padding;
	*(int32_t *)p = htonl(this->Stratagus);
	p += 4;
	*(int32_t *)p = htonl(this->Version);
	p += 4;
	*(uint32_t *)p = htonl(this->MapUID);
	p += 4;
	*(int32_t *)p = htonl(this->Lag);
	p += 4;
	*(int32_t *)p = htonl(this->Updates);
	p += 4;

	switch (this->SubType) {
		case ICMHello:
		case ICMConfig:
		case ICMWelcome:
		case ICMResync:
		case ICMGo:
			for (int i = 0; i < PlayerMax; ++i) {
				const unsigned char *x = this->u.Hosts[i].Serialize();
				memcpy(p, x, CNetworkHost::Size());
				p += CNetworkHost::Size();
				delete[] x;
			}
			break;
		case ICMMap:
			memcpy(p, this->u.MapPath, sizeof(this->u.MapPath));
			p += sizeof(this->u.MapPath);
			break;
		case ICMState: {
			const unsigned char *x = this->u.State.Serialize();
			memcpy(p, x, CServerSetup::Size());
			p += CServerSetup::Size();
			delete[] x;
			break;
		}
	}
	return buf;
}

void CInitMessage::Deserialize(const unsigned char *p)
{
	this->Type = *p++;
	this->SubType = *p++;
	this->HostsCount = *p++;
	this->padding = *p++;
	this->Stratagus = ntohl(*(int32_t *)p);
	p += 4;
	this->Version = ntohl(*(int32_t *)p);
	p += 4;
	this->MapUID = ntohl(*(uint32_t *)p);
	p += 4;
	this->Lag = ntohl(*(int32_t *)p);
	p += 4;
	this->Updates = ntohl(*(int32_t *)p);
	p += 4;

	switch (this->SubType) {
		case ICMHello:
		case ICMConfig:
		case ICMWelcome:
		case ICMResync:
		case ICMGo:
			for (int i = 0; i < PlayerMax; ++i) {
				this->u.Hosts[i].Deserialize(p);
				p += CNetworkHost::Size();
			}
			break;
		case ICMMap:
			memcpy(this->u.MapPath, p, sizeof(this->u.MapPath));
			p += sizeof(this->u.MapPath);
			break;
		case ICMState:
			this->u.State.Deserialize(p);
			p += CServerSetup::Size();
			break;
	}
}

//----------------------------------------------------------------------------
// Functions
//----------------------------------------------------------------------------

int FindHostIndexBy(unsigned long ip, int port)
{
	for (int i = 0; i != PlayerMax; ++i) {
		if (Hosts[i].Host == ip && Hosts[i].Port == port) {
			return i;
		}
	}
	return -1;
}

/**
** Send an InitConfig message across the Network
**
** @param host Host to send to (network byte order).
** @param port Port of host to send to (network byte order).
** @param msg The message to send
**
** @todo FIXME: we don't need to put the header into all messages.
** (header = msg->Stratagus ... )
*/
static int NetworkSendICMessage(unsigned long host, int port, const CInitMessage &msg)
{
	const unsigned char *buf = msg.Serialize();
	int ret = NetSendUDP(NetworkFildes, host, port, buf, CInitMessage::Size());
	delete[] buf;
	return ret;
}

#ifdef DEBUG
static const char *ncconstatenames[] = {
	"ccs_unused",
	"ccs_connecting",          // new client
	"ccs_connected",           // has received slot info
	"ccs_mapinfo",             // has received matching map-info
	"ccs_badmap",              // has received non-matching map-info
	"ccs_synced",              // client is in sync with server
	"ccs_async",               // server user has changed selection
	"ccs_changed",             // client user has made menu selection
	"ccs_detaching",           // client user wants to detach
	"ccs_disconnected",        // client has detached
	"ccs_unreachable",         // server is unreachable
	"ccs_usercanceled",        // user canceled game
	"ccs_nofreeslots",         // server has no more free slots
	"ccs_serverquits",         // server quits
	"ccs_goahead",             // server wants to start game
	"ccs_started",             // server has started game
	"ccs_incompatibleengine",  // incompatible engine version
	"ccs_incompatiblenetwork", // incompatible network version
};

static const char *icmsgsubtypenames[] = {
	"Hello",                   // Client Request
	"Config",                  // Setup message configure clients

	"EngineMismatch",          // Stratagus engine version doesn't match
	"ProtocolMismatch",        // Network protocol version doesn't match
	"EngineConfMismatch",      // Engine configuration isn't identical
	"MapUidMismatch",          // MAP UID doesn't match

	"GameFull",                // No player slots available
	"Welcome",                 // Acknowledge for new client connections

	"Waiting",                 // Client has received Welcome and is waiting for Map/State
	"Map",                     // MapInfo (and Mapinfo Ack)
	"State",                   // StateInfo
	"Resync",                  // Ack StateInfo change

	"ServerQuit",              // Server has quit game
	"GoodBye",                 // Client wants to leave game
	"SeeYou",                  // Client has left game

	"Go",                      // Client is ready to run
	"AreYouThere",             // Server asks are you there
	"IAmHere",                 // Client answers I am here
};
#endif

static void NetworkSendICMessage_Log(unsigned long ip, int port, const CInitMessage &msg)
{
	const int n = NetworkSendICMessage(ip, port, msg);

	UNUSED(n);
	DebugPrint("Sending to %d.%d.%d.%d:%d -> %d:%s(%d)(size=%d)\n"
				_C_ NIPQUAD(ntohl(ip)) _C_ ntohs(port)
				_C_ msg.Type _C_ icmsgsubtypenames[msg.SubType] _C_ msg.SubType _C_ n);
}

/**
** Send a message to the server, but only if the last packet was a while
** ago
**
** @param msg    The message to send
** @param msecs  microseconds to delay
*/
static void NetworkSendRateLimitedClientMessage(const CInitMessage &msg, unsigned long msecs)
{
	const unsigned long now = GetTicks();
	if (now - NetLastPacketSent >= msecs) {
		NetLastPacketSent = now;
		if (msg.SubType == LastStateMsgType) {
			++NetStateMsgCnt;
		} else {
			NetStateMsgCnt = 0;
			LastStateMsgType = msg.SubType;
		}
		const int n = NetworkSendICMessage(NetworkServerIP, htons(NetworkServerPort), msg);
		UNUSED(n); // not used in release
		if (!NetStateMsgCnt) {
			DebugPrint
			("Sending Init Message (%s:%d): %d:%d(%d) %d.%d.%d.%d:%d\n" _C_
			 ncconstatenames[NetLocalState] _C_ NetStateMsgCnt _C_
			 msg.Type _C_ msg.SubType _C_ n _C_
			 NIPQUAD(ntohl(NetworkServerIP)) _C_ NetworkServerPort);
		}
	}
}

/**
** Setup the IP-Address of the network server to connect to
**
** @param serveraddr the serveraddress the user has entered
**
** @return True, if error; otherwise false.
*/
int NetworkSetupServerAddress(const std::string &serveraddr, int port)
{
	unsigned long addr = NetResolveHost(serveraddr);

	if (addr == INADDR_NONE) {
		return 1;
	}
	NetworkServerIP = addr;
	if (port == 0) {
		port = NetworkServerPort;
	}
	NetworkServerPort = port;
	DebugPrint("SELECTED SERVER: %s:%d (%d.%d.%d.%d)\n" _C_ serveraddr.c_str() _C_ NetworkServerPort _C_ NIPQUAD(ntohl(addr)));
	return 0;
}

/**
** Setup Network connect state machine for clients
*/
void NetworkInitClientConnect()
{
	NetConnectRunning = 2;
	NetLastPacketSent = GetTicks();
	NetLocalState = ccs_connecting;
	NetStateMsgCnt = 0;
	LastStateMsgType = ICMServerQuit;
	for (int i = 0; i < PlayerMax; ++i) {
		Hosts[i].Clear();
	}
	ServerSetupState.Clear();
	LocalSetupState.Clear();
}

/**
** Terminate Network connect state machine for clients
*/
void NetworkExitClientConnect()
{
	NetConnectRunning = 0;
	NetPlayers = 0; // Make single player menus work again!
}

/**
** Terminate and detach Network connect state machine for the client
*/
void NetworkDetachFromServer()
{
	NetLocalState = ccs_detaching;
	NetStateMsgCnt = 0;
}

/**
** Setup Network connect state machine for the server
*/
void NetworkInitServerConnect(int openslots)
{
	NetConnectRunning = 1;

	// Cannot use NetPlayers here, as map change might modify the number!!
	for (int i = 0; i < PlayerMax; ++i) {
		NetStates[i].State = ccs_unused;
		Hosts[i].Clear();
	}

	// preset the server (initially always slot 0)
	Hosts[0].SetName(Parameters::Instance.LocalPlayerName.c_str());

	ServerSetupState.Clear();
	LocalSetupState.Clear();
	for (int i = openslots; i < PlayerMax - 1; ++i) {
		ServerSetupState.CompOpt[i] = 1;
	}
}

/**
** Terminate Network connect state machine for the server
*/
void NetworkExitServerConnect()
{
	CInitMessage message;

	message.Type = MessageInitReply;
	message.SubType = ICMServerQuit;
	for (int h = 1; h < PlayerMax - 1; ++h) {
		// Spew out 5 and trust in God that they arrive
		// Clients will time out otherwise anyway
		if (Hosts[h].PlyNr) {
			for (int i = 0; i < 5; ++i) {
				NetworkSendICMessage_Log(Hosts[h].Host, Hosts[h].Port, message);
			}
		}
	}

	NetworkInitServerConnect(0); // Reset Hosts slots
	NetConnectRunning = 0;
}

/**
** Notify state change by menu user to connected clients
*/
void NetworkServerResyncClients()
{
	if (NetConnectRunning) {
		for (int i = 1; i < PlayerMax - 1; ++i) {
			if (Hosts[i].PlyNr && NetStates[i].State == ccs_synced) {
				NetStates[i].State = ccs_async;
			}
		}
	}
}

/**
** Server user has finally hit the start game button
*/
void NetworkServerStartGame()
{
	Assert(ServerSetupState.CompOpt[0] == 0);

	// save it first..
	LocalSetupState = ServerSetupState;

	// Make a list of the available player slots.
	int num[PlayerMax];
	int rev[PlayerMax];
	int h = 0;
	for (int i = 0; i < PlayerMax; ++i) {
		if (Map.Info.PlayerType[i] == PlayerPerson) {
			rev[i] = h;
			num[h++] = i;
			DebugPrint("Slot %d is available for an interactive player (%d)\n" _C_ i _C_ rev[i]);
		}
	}
	// Make a list of the available computer slots.
	int n = h;
	for (int i = 0; i < PlayerMax; ++i) {
		if (Map.Info.PlayerType[i] == PlayerComputer) {
			rev[i] = n++;
			DebugPrint("Slot %d is available for an ai computer player (%d)\n" _C_ i _C_ rev[i]);
		}
	}
	// Make a list of the remaining slots.
	for (int i = 0; i < PlayerMax; ++i) {
		if (Map.Info.PlayerType[i] != PlayerPerson
			&& Map.Info.PlayerType[i] != PlayerComputer) {
			rev[i] = n++;
			// PlayerNobody - not available to anything..
		}
	}

#if 0
	printf("INITIAL ServerSetupState:\n");
	for (int i = 0; i < PlayerMax - 1; ++i) {
		printf("%02d: CO: %d   Race: %d   Host: ", i, ServerSetupState.CompOpt[i], ServerSetupState.Race[i]);
		if (ServerSetupState.CompOpt[i] == 0) {
			printf(" %d.%d.%d.%d:%d %s", NIPQUAD(ntohl(Hosts[i].Host)), ntohs(Hosts[i].Port), Hosts[i].PlyName);
		}
		printf("\n");
	}
#endif

	int org[PlayerMax];
	// Reverse to assign slots to menu setup state positions.
	for (int i = 0; i < PlayerMax; ++i) {
		org[i] = -1;
		for (int j = 0; j < PlayerMax; ++j) {
			if (rev[j] == i) {
				org[i] = j;
				break;
			}
		}
	}

	// Calculate NetPlayers
	NetPlayers = h;
	for (int i = 1; i < h; ++i) {
		if (Hosts[i].PlyNr == 0 && ServerSetupState.CompOpt[i] != 0) {
			NetPlayers--;
		} else if (Hosts[i].PlyName[0] == 0) {
			// Unused slot gets a computer player
			ServerSetupState.CompOpt[i] = 1;
			LocalSetupState.CompOpt[i] = 1;
			NetPlayers--;
		}
	}

	// Compact host list.. (account for computer/closed slots in the middle..)
	for (int i = 1; i < h; ++i) {
		if (Hosts[i].PlyNr == 0) {
			int j;
			for (j = i + 1; j < PlayerMax - 1; ++j) {
				if (Hosts[j].PlyNr) {
					DebugPrint("Compact: Hosts %d -> Hosts %d\n" _C_ j _C_ i);
					Hosts[i] = Hosts[j];
					Hosts[j].Clear();
					std::swap(LocalSetupState.CompOpt[i], LocalSetupState.CompOpt[j]);
					std::swap(LocalSetupState.Race[i], LocalSetupState.Race[j]);
					std::swap(LocalSetupState.LastFrame[i], LocalSetupState.LastFrame[j]);
					break;
				}
			}
			if (j == PlayerMax - 1) {
				break;
			}
		}
	}

	// Randomize the position.
	// It can be disabled by writing NoRandomPlacementMultiplayer() in lua files.
	// Players slots are then mapped to players numbers(and colors).

	if (NoRandomPlacementMultiplayer == 1) {
		for (int i = 0; i < PlayerMax; ++i) {
			if (Map.Info.PlayerType[i] != PlayerComputer) {
				org[i] = Hosts[i].PlyNr;
			}
		}
	} else {
		int j = h;
		for (int i = 0; i < NetPlayers; ++i) {
			Assert(j > 0);
			int chosen = MyRand() % j;

			n = num[chosen];
			Hosts[i].PlyNr = n;
			int k = org[i];
			if (k != n) {
				for (int o = 0; o < PlayerMax; ++o) {
					if (org[o] == n) {
						org[o] = k;
						break;
					}
				}
				org[i] = n;
			}
			DebugPrint("Assigning player %d to slot %d (%d)\n" _C_ i _C_ n _C_ org[i]);

			num[chosen] = num[--j];
		}
	}

	// Complete all setup states for the assigned slots.
	for (int i = 0; i < PlayerMax; ++i) {
		num[i] = 1;
		n = org[i];
		ServerSetupState.CompOpt[n] = LocalSetupState.CompOpt[i];
		ServerSetupState.Race[n] = LocalSetupState.Race[i];
		ServerSetupState.LastFrame[n] = LocalSetupState.LastFrame[i];
	}

	/* NOW we have NetPlayers in Hosts array, with ServerSetupState shuffled up to match it.. */

	//
	// Send all clients host:ports to all clients.
	//  Slot 0 is the server!
	//
	NetLocalPlayerNumber = Hosts[0].PlyNr;
	HostsCount = NetPlayers - 1;

	// Move ourselves (server slot 0) to the end of the list
	std::swap(Hosts[0], Hosts[HostsCount]);

	// Prepare the final config message:
	CInitMessage message;
	message.Type = MessageInitReply;
	message.SubType = ICMConfig;
	message.HostsCount = NetPlayers;
	message.MapUID = Map.Info.MapUID;
	for (int i = 0; i < NetPlayers; ++i) {
		message.u.Hosts[i] = Hosts[i];
		message.u.Hosts[i].PlyNr = Hosts[i].PlyNr;
	}

	// Prepare the final state message:
	CInitMessage statemsg;
	statemsg.Type = MessageInitReply;
	statemsg.SubType = ICMState;
	statemsg.HostsCount = NetPlayers;
	statemsg.u.State = ServerSetupState;
	statemsg.MapUID = Map.Info.MapUID;

	DebugPrint("Ready, sending InitConfig to %d host(s)\n" _C_ HostsCount);
	// Send all clients host:ports to all clients.
	for (int j = HostsCount; j;) {

breakout:
		// Send to all clients.
		for (int i = 0; i < HostsCount; ++i) {
			if (num[Hosts[i].PlyNr] == 1) { // not acknowledged yet
				unsigned long host = message.u.Hosts[i].Host;
				int port = message.u.Hosts[i].Port;
				message.u.Hosts[i].Host = message.u.Hosts[i].Port = 0;
				NetworkSendICMessage_Log(host, port, message);
				message.u.Hosts[i].Host = host;
				message.u.Hosts[i].Port = port;
			} else if (num[Hosts[i].PlyNr] == 2) {
				unsigned long host = message.u.Hosts[i].Host;
				int port = message.u.Hosts[i].Port;
				NetworkSendICMessage_Log(host, port, statemsg);
			}
		}

		// Wait for acknowledge
		unsigned char buf[1024];
		while (j && NetSocketReady(NetworkFildes, 1000)) {
			unsigned long host;
			int port;
			const int len = NetRecvUDP(NetworkFildes, buf, sizeof(buf), &host, &port);
			if (len < 0) {
				DebugPrint("*Receive ack failed: (%d) from %d.%d.%d.%d:%d\n" _C_
						   len _C_ NIPQUAD(ntohl(host)) _C_ ntohs(port));
				continue;
			}

			if (len != (int)CInitMessage::Size()) {
				DebugPrint("Unexpected message size\n");
				continue;
			}
			CInitMessage msg;

			msg.Deserialize(buf);
			if (msg.Type == MessageInitHello) {
				switch (msg.SubType) {
					case ICMConfig: {
						DebugPrint("Got ack for InitConfig from %d.%d.%d.%d:%d\n"
								   _C_ NIPQUAD(ntohl(host)) _C_ ntohs(port));

						const int index = FindHostIndexBy(host, port);
						if (index != -1) {
							if (num[Hosts[index].PlyNr] == 1) {
								num[Hosts[index].PlyNr]++;
							}
							goto breakout;
						}
						break;
					}
					case ICMGo: {
						DebugPrint("Got ack for InitState from %d.%d.%d.%d:%d\n"
								   _C_ NIPQUAD(ntohl(host)) _C_ ntohs(port));

						const int index = FindHostIndexBy(host, port);
						if (index != -1) {
							if (num[Hosts[index].PlyNr] == 2) {
								num[Hosts[index].PlyNr] = 0;
								--j;
								DebugPrint("Removing host %d from waiting list\n" _C_ j);
							}
						}
						break;
					}
					default:
						DebugPrint("Server: Config ACK: Unhandled subtype %d\n" _C_ msg.SubType);
						break;
				}
			} else {
				DebugPrint("Unexpected Message Type %d while waiting for Config ACK\n" _C_ msg.Type);
			}
		}
	}

	DebugPrint("DONE: All configs acked - Now starting..\n");

	// Give clients a quick-start kick..
	message.SubType = ICMGo;
	for (int i = 0; i < HostsCount; ++i) {
		const unsigned long host = message.u.Hosts[i].Host;
		const int port = message.u.Hosts[i].Port;
		NetworkSendICMessage_Log(host, port, message);
	}
}

/**
** Multiplayer network game final race and player type setup.
*/
void NetworkGamePrepareGameSettings()
{
	DebugPrint("NetPlayers = %d\n" _C_ NetPlayers);

	GameSettings.NetGameType = SettingsMultiPlayerGame;

#ifdef DEBUG
	for (int i = 0; i < PlayerMax - 1; i++) {
		printf("%02d: CO: %d   Race: %d   Host: ", i, ServerSetupState.CompOpt[i], ServerSetupState.Race[i]);
		if (ServerSetupState.CompOpt[i] == 0) {
			for (int h = 0; h < NetPlayers; h++) {
				if (Hosts[h].PlyNr == i) {
					printf("%s", Hosts[h].PlyName);
				}
			}
		}
		printf("\n");
	}
#endif

	// Make a list of the available player slots.
	int num[PlayerMax];
	int comp[PlayerMax];
	int c = 0;
	int h = 0;
	for (int i = 0; i < PlayerMax; i++) {
		if (Map.Info.PlayerType[i] == PlayerPerson) {
			num[h++] = i;
		}
		if (Map.Info.PlayerType[i] == PlayerComputer) {
			comp[c++] = i; // available computer player slots
		}
	}
	for (int i = 0; i < h; i++) {
		switch (ServerSetupState.CompOpt[num[i]]) {
			case 0: {
				GameSettings.Presets[num[i]].Type = PlayerPerson;
				int v = ServerSetupState.Race[num[i]];
				if (v != 0) {
					int x = 0;

					for (unsigned int n = 0; n < PlayerRaces.Count; ++n) {
						if (PlayerRaces.Visible[n]) {
							if (x + 1 == v) {
								break;
							}
							++x;
						}
					}
					GameSettings.Presets[num[i]].Race = x;
				} else {
					GameSettings.Presets[num[i]].Race = SettingsPresetMapDefault;
				}
				break;
			}
			case 1:
				GameSettings.Presets[num[i]].Type = PlayerComputer;
				break;
			case 2:
				GameSettings.Presets[num[i]].Type = PlayerNobody;
			default:
				break;
		}
	}
	for (int i = 0; i < c; i++) {
		if (ServerSetupState.CompOpt[comp[i]] == 2) { // closed..
			GameSettings.Presets[comp[i]].Type = PlayerNobody;
			DebugPrint("Settings[%d].Type == Closed\n" _C_ comp[i]);
		}
	}

#ifdef DEBUG
	for (int i = 0; i < NetPlayers; i++) {
		Assert(GameSettings.Presets[Hosts[i].PlyNr].Type == PlayerPerson);
	}
#endif
}

/**
** Assign player slots and names in a network game..
*/
void NetworkConnectSetupGame()
{
	ThisPlayer->SetName(Parameters::Instance.LocalPlayerName);
	for (int i = 0; i < HostsCount; ++i) {
		Players[Hosts[i].PlyNr].SetName(Hosts[i].PlyName);
	}
}

/**
** Callback from netconnect loop in Client-Sync state:
** Compare local state with server's information
** and force update when changes have occured.
*/
static void NetClientCheckLocalState()
{
	if (LocalSetupState.Ready[NetLocalHostsSlot] != ServerSetupState.Ready[NetLocalHostsSlot]) {
		NetLocalState = ccs_changed;
		return;
	}
	if (LocalSetupState.Race[NetLocalHostsSlot] != ServerSetupState.Race[NetLocalHostsSlot]) {
		NetLocalState = ccs_changed;
		return;
	}
}

/**
** Client Menu Loop: Send out client request messages
*/
void NetworkProcessClientRequest()
{
	CInitMessage message;

changed:
	switch (NetLocalState) {
		case ccs_disconnected:
			message.Type = MessageInitHello;
			message.SubType = ICMSeeYou;
			// Spew out 5 and trust in God that they arrive
			for (int i = 0; i < 5; ++i) {
				NetworkSendICMessage(NetworkServerIP, htons(NetworkServerPort), message);
			}
			NetLocalState = ccs_usercanceled;
			NetConnectRunning = 0; // End the menu..
			break;
		case ccs_detaching:
			if (NetStateMsgCnt < 10) { // 10 retries = 1 second
				message.Type = MessageInitHello;
				message.SubType = ICMGoodBye;
				NetworkSendRateLimitedClientMessage(message, 100);
			} else {
				// Server is ignoring us - break out!
				NetLocalState = ccs_unreachable;
				NetConnectRunning = 0; // End the menu..
				DebugPrint("ccs_detaching: Above message limit %d\n" _C_ NetStateMsgCnt);
			}
			break;
		case ccs_connecting: // connect to server
			if (NetStateMsgCnt < 48) { // 48 retries = 24 seconds
				message.Type = MessageInitHello;
				message.SubType = ICMHello;
				message.u.Hosts[0].SetName(Parameters::Instance.LocalPlayerName.c_str());
				message.MapUID = 0L;
				NetworkSendRateLimitedClientMessage(message, 500);
			} else {
				NetLocalState = ccs_unreachable;
				NetConnectRunning = 0; // End the menu..
				DebugPrint("ccs_connecting: Above message limit %d\n" _C_ NetStateMsgCnt);
			}
			break;
		case ccs_connected:
			if (NetStateMsgCnt < 20) { // 20 retries
				message.Type = MessageInitHello;
				message.SubType = ICMWaiting;
				NetworkSendRateLimitedClientMessage(message, 650);
			} else {
				NetLocalState = ccs_unreachable;
				NetConnectRunning = 0; // End the menu..
				DebugPrint("ccs_connected: Above message limit %d\n" _C_ NetStateMsgCnt);
			}
			break;
		case ccs_synced:
			NetClientCheckLocalState();
			if (NetLocalState != ccs_synced) {
				NetStateMsgCnt = 0;
				goto changed;
			}
			message.Type = MessageInitHello;
			message.SubType = ICMWaiting;
			NetworkSendRateLimitedClientMessage(message, 850);
			break;
		case ccs_changed:
			if (NetStateMsgCnt < 20) { // 20 retries
				message.Type = MessageInitHello;
				message.SubType = ICMState;
				message.u.State = LocalSetupState;
				message.MapUID = Map.Info.MapUID;
				NetworkSendRateLimitedClientMessage(message, 450);
			} else {
				NetLocalState = ccs_unreachable;
				NetConnectRunning = 0; // End the menu..
				DebugPrint("ccs_changed: Above message limit %d\n" _C_ NetStateMsgCnt);
			}
			break;
		case ccs_async:
			if (NetStateMsgCnt < 20) { // 20 retries
				message.Type = MessageInitHello;
				message.SubType = ICMResync;
				NetworkSendRateLimitedClientMessage(message, 450);
			} else {
				NetLocalState = ccs_unreachable;
				NetConnectRunning = 0; // End the menu..
				DebugPrint("ccs_async: Above message limit %d\n" _C_ NetStateMsgCnt);
			}
			break;
		case ccs_mapinfo:
			if (NetStateMsgCnt < 20) { // 20 retries
				message.Type = MessageInitHello;
				message.SubType = ICMMap; // ICMMapAck..
				message.MapUID = Map.Info.MapUID;
				NetworkSendRateLimitedClientMessage(message, 650);
			} else {
				NetLocalState = ccs_unreachable;
				NetConnectRunning = 0; // End the menu..
				DebugPrint("ccs_mapinfo: Above message limit %d\n" _C_ NetStateMsgCnt);
			}
		case ccs_badmap:
			if (NetStateMsgCnt < 20) { // 20 retries
				message.Type = MessageInitHello;
				message.SubType = ICMMapUidMismatch;
				message.MapUID = Map.Info.MapUID; // MAP Uid doesn't match
				NetworkSendRateLimitedClientMessage(message, 650);
			} else {
				NetLocalState = ccs_unreachable;
				NetConnectRunning = 0; // End the menu..
				DebugPrint("ccs_badmap: Above message limit %d\n" _C_ NetStateMsgCnt);
			}
			break;
		case ccs_goahead:
			if (NetStateMsgCnt < 50) { // 50 retries
				message.Type = MessageInitHello;
				message.SubType = ICMConfig;
				NetworkSendRateLimitedClientMessage(message, 250);
			} else {
				NetLocalState = ccs_unreachable;
				NetConnectRunning = 0; // End the menu..
				DebugPrint("ccs_goahead: Above message limit %d\n" _C_ NetStateMsgCnt);
			}
		case ccs_started:
			if (NetStateMsgCnt < 20) { // 20 retries
				message.Type = MessageInitHello;
				message.SubType = ICMGo;
				NetworkSendRateLimitedClientMessage(message, 250);
			} else {
				NetConnectRunning = 0; // End the menu..
			}
			break;
		default:
			break;
	}
}

/**
** Kick a client that doesn't answer to our packets
**
** @param c The client (host slot) to kick
*/
static void KickDeadClient(int c)
{
	DebugPrint("kicking client %d\n" _C_ Hosts[c].PlyNr);
	NetStates[c].State = ccs_unused;
	Hosts[c].Clear();
	ServerSetupState.Ready[c] = 0;
	ServerSetupState.Race[c] = 0;
	ServerSetupState.LastFrame[c] = 0L;

	// Resync other clients
	for (int n = 1; n < PlayerMax - 1; ++n) {
		if (n != c && Hosts[n].PlyNr) {
			NetStates[n].State = ccs_async;
		}
	}
}

/**
** Server Menu Loop: Send out server request messages
*/
void NetworkProcessServerRequest()
{
	if (GameRunning) {
		return;
		// Game already started...
	}
	CInitMessage message;

	for (int i = 1; i < PlayerMax - 1; ++i) {
		if (Hosts[i].PlyNr && Hosts[i].Host && Hosts[i].Port) {
			const unsigned long fcd = FrameCounter - ServerSetupState.LastFrame[i];
			if (fcd >= CLIENT_LIVE_BEAT) {
				if (fcd > CLIENT_IS_DEAD) {
					KickDeadClient(i);
				} else if (fcd % 5 == 0) {
					message.Type = MessageInitReply;
					message.SubType = ICMAYT; // Probe for the client
					message.MapUID = 0L;
					const int n = NetworkSendICMessage(Hosts[i].Host, Hosts[i].Port, message);

					UNUSED(n); // unused in release
					DebugPrint("Sending InitReply Message AreYouThere: (%d) to %d.%d.%d.%d:%d (%ld:%ld)\n" _C_
							   n _C_ NIPQUAD(ntohl(Hosts[i].Host)) _C_ ntohs(Hosts[i].Port) _C_
							   FrameCounter _C_(unsigned long)ServerSetupState.LastFrame[i]);
				}
			}
		}
	}
}

#ifdef DEBUG
/**
** Parse a network menu packet in client disconnected state.
**
** @param msg message received
*/
static void ClientParseDisconnected(const CInitMessage &msg)
{
	DebugPrint("ccs_disconnected: Server sending GoodBye dups %d\n" _C_ msg.SubType);
}
#endif

/**
** Parse a network menu packet in client detaching state.
**
** @param msg message received
*/
static void ClientParseDetaching(const CInitMessage &msg)
{
	switch (msg.SubType) {

		case ICMGoodBye: // Server has let us go
			NetLocalState = ccs_disconnected;
			NetStateMsgCnt = 0;
			break;

		default:
			DebugPrint("ccs_detaching: Unhandled subtype %d\n" _C_ msg.SubType);
			break;
	}
}

/**
**  Parse a network menu packet in client connecting state.
**
**  @param msg   message received
**  @param host  host which send the message
**  @param port  port from where the messahe nas been sent
*/
static void ClientParseConnecting(const CInitMessage &msg, unsigned long host, int port)
{
	switch (msg.SubType) {

		case ICMEngineMismatch: // Stratagus engine version doesn't match
			fprintf(stderr, "Incompatible Stratagus version "
					"%d <-> %d\n"
					"from %d.%d.%d.%d:%d\n",
					msg.Stratagus,
					StratagusVersion,
					NIPQUAD(ntohl(host)), ntohs(port));
			NetLocalState = ccs_incompatibleengine;
			NetConnectRunning = 0; // End the menu..
			return;

		case ICMProtocolMismatch: // Network protocol version doesn't match
			fprintf(stderr, "Incompatible network protocol version "
					NetworkProtocolFormatString " <-> "
					NetworkProtocolFormatString "\n"
					"from %d.%d.%d.%d:%d\n",
					NetworkProtocolFormatArgs(msg.Version),
					NetworkProtocolFormatArgs(NetworkProtocolVersion),
					NIPQUAD(ntohl(host)), ntohs(port));
			NetLocalState = ccs_incompatiblenetwork;
			NetConnectRunning = 0; // End the menu..
			return;

		case ICMGameFull: // Game is full - server rejected connnection
			fprintf(stderr, "Server at %d.%d.%d.%d:%d is full!\n",
					NIPQUAD(ntohl(host)), ntohs(port));
			NetLocalState = ccs_nofreeslots;
			NetConnectRunning = 0; // End the menu..
			return;

		case ICMWelcome: // Server has accepted us
			NetLocalState = ccs_connected;
			NetStateMsgCnt = 0;
			NetLocalHostsSlot = msg.u.Hosts[0].PlyNr;
			memcpy(Hosts[0].PlyName, msg.u.Hosts[0].PlyName, sizeof(Hosts[0].PlyName) - 1); // Name of server player
			NetworkLag = msg.Lag;
			NetworkUpdates = msg.Updates;

			Hosts[0].Host = NetworkServerIP;
			Hosts[0].Port = htons(NetworkServerPort);
			for (int i = 1; i < PlayerMax; ++i) {
				if (i != NetLocalHostsSlot) {
					Hosts[i] = msg.u.Hosts[i];
				} else {
					Hosts[i].PlyNr = i;
					Hosts[i].SetName(Parameters::Instance.LocalPlayerName.c_str());
				}
			}
			break;

		default:
			DebugPrint("ccs_connecting: Unhandled subtype %d\n" _C_ msg.SubType);
			break;
	}
}

/**
** Check if the map name looks safe.
**
** A map name looks safe when there are no special characters
** and no .. or // sequences. This way only real valid
** maps from the map directory will be loaded.
**
** @return  true if the map name looks safe.
*/
static bool IsSafeMapName(const char *mapname)
{
	char buf[256];

	if (strncpy_s(buf, sizeof(buf), mapname, sizeof(buf)) != 0) {
		return false;
	}
	if (strstr(buf, "..")) {
		return false;
	}
	if (strstr(buf, "//")) {
		return false;
	}
	if (buf[0] == '\0') {
		return false;
	}

	for (const char *ch = buf; *ch != '\0'; ++ch) {
		if (!isalnum(*ch) && *ch != '/' && *ch != '.' && *ch != '-'
			&& *ch != '(' && *ch != ')' && *ch != '_') {
			return false;
		}
	}
	return true;
}

/**
** Parse a network menu packet in client connected state.
**
** @param msg message received
*/
static void ClientParseConnected(const CInitMessage &msg)
{
	switch (msg.SubType) {
		case ICMMap: { // Server has sent us new map info
			if (!IsSafeMapName(msg.u.MapPath)) {
				fprintf(stderr, "Unsecure map name!\n");
				NetLocalState = ccs_badmap;
				break;
			}
			NetworkMapName = std::string(msg.u.MapPath, sizeof(msg.u.MapPath));
			std::string mappath = StratagusLibPath + "/" + NetworkMapName;
			LoadStratagusMapInfo(mappath);
			if (msg.MapUID != Map.Info.MapUID) {
				NetLocalState = ccs_badmap;
				fprintf(stderr, "Stratagus maps do not match (0x%08x) <-> (0x%08x)\n",
						(unsigned int)Map.Info.MapUID,
						(unsigned int)msg.MapUID);
				break;
			}
			NetLocalState = ccs_mapinfo;
			NetStateMsgCnt = 0;
			break;
		}

		case ICMWelcome: // Server has accepted us (dup)
			break;

		default:
			DebugPrint("ccs_connected: Unhandled subtype %d\n" _C_ msg.SubType);
			break;
	}
}

/**
** Parse a network menu packet in client initial mapinfo state.
**
** @param msg message received
*/
static void ClientParseMapInfo(const CInitMessage &msg)
{
	switch (msg.SubType) {
		case ICMState: // Server has sent us first state info
			ServerSetupState = msg.u.State;
			NetLocalState = ccs_synced;
			NetStateMsgCnt = 0;
			break;

		default:
			DebugPrint("ccs_mapinfo: Unhandled subtype %d\n" _C_ msg.SubType);
			break;
	}
}

/**
**  Parse a network menu packet in client synced state.
**
**  @param msg   message received
**  @param host  host which send the message
**  @param port  port from where the messahe nas been sent
*/
static void ClientParseSynced(const CInitMessage &msg, unsigned long host, int port)
{
	switch (msg.SubType) {
		case ICMState: // Server has sent us new state info
			DebugPrint("ccs_synced: ICMState received\n");
			ServerSetupState = msg.u.State;
			NetLocalState = ccs_async;
			NetStateMsgCnt = 0;
			break;

		case ICMConfig: { // Server gives the go ahead.. - start game
			DebugPrint("ccs_synced: Config subtype %d received - starting\n" _C_ msg.SubType);
			HostsCount = 0;
			for (int i = 0; i < msg.HostsCount - 1; ++i) {
				if (msg.u.Hosts[i].Host || msg.u.Hosts[i].Port) {
					Hosts[HostsCount] = msg.u.Hosts[i];
					HostsCount++;
					DebugPrint("Client %d = %d.%d.%d.%d:%d [%.*s]\n" _C_
							   msg.u.Hosts[i].PlyNr _C_ NIPQUAD(ntohl(msg.u.Hosts[i].Host)) _C_
							   ntohs(msg.u.Hosts[i].Port) _C_
							   static_cast<int>(sizeof(msg.u.Hosts[i].PlyName)) _C_
							   msg.u.Hosts[i].PlyName);
				} else { // Own client
					NetLocalPlayerNumber = msg.u.Hosts[i].PlyNr;
					DebugPrint("SELF %d [%.*s]\n" _C_ msg.u.Hosts[i].PlyNr _C_
							   static_cast<int>(sizeof(msg.u.Hosts[i].PlyName)) _C_
							   msg.u.Hosts[i].PlyName);
				}
			}
			// server is last:
			Hosts[HostsCount].Host = host;
			Hosts[HostsCount].Port = port;
			Hosts[HostsCount].PlyNr = msg.u.Hosts[msg.HostsCount - 1].PlyNr;
			Hosts[HostsCount].SetName(msg.u.Hosts[msg.HostsCount - 1].PlyName);
			++HostsCount;
			NetPlayers = HostsCount + 1;
			DebugPrint("Server %d = %d.%d.%d.%d:%d [%.*s]\n" _C_
					   msg.u.Hosts[i].PlyNr _C_ NIPQUAD(ntohl(host)) _C_
					   ntohs(port) _C_
					   static_cast<int>(sizeof(msg.u.Hosts[i].PlyName)) _C_
					   msg.u.Hosts[i].PlyName);

			// put ourselves to the end, like on the server..
			Hosts[HostsCount].Host = 0;
			Hosts[HostsCount].Port = 0;
			Hosts[HostsCount].PlyNr = NetLocalPlayerNumber;
			Hosts[HostsCount].SetName(Parameters::Instance.LocalPlayerName.c_str());

			NetLocalState = ccs_goahead;
			NetStateMsgCnt = 0;
			break;
		}
		default:
			DebugPrint("ccs_synced: Unhandled subtype %d\n" _C_ msg.SubType);
			break;
	}
}

/**
** Parse a network menu packet in client async state.
**
** @param msg message received
*/
static void ClientParseAsync(const CInitMessage &msg)
{
	switch (msg.SubType) {
		case ICMResync: // Server has resynced with us and sends resync data
			DebugPrint("ccs_async: ICMResync\n");
			for (int i = 1; i < PlayerMax - 1; ++i) {
				if (i != NetLocalHostsSlot) {
					Hosts[i] = msg.u.Hosts[i];
				} else {
					Hosts[i].PlyNr = msg.u.Hosts[i].PlyNr;
					Hosts[i].SetName(Parameters::Instance.LocalPlayerName.c_str());
				}
			}
			NetLocalState = ccs_synced;
			NetStateMsgCnt = 0;
			break;

		default:
			DebugPrint("ccs_async: Unhandled subtype %d\n" _C_ msg.SubType);
			break;
	}
}

/**
** Parse a network menu packet in client final goahead waiting state.
**
** @param msg message received
*/
static void ClientParseGoAhead(const CInitMessage &msg)
{
	switch (msg.SubType) {

		case ICMConfig: // Server go ahead dup - ignore..
			break;

		case ICMState: // Server has sent final state info
			DebugPrint("ccs_goahead: Final State subtype %d received - starting\n" _C_ msg.SubType);
			ServerSetupState = msg.u.State;
			NetLocalState = ccs_started;
			NetStateMsgCnt = 0;
			break;

		default:
			DebugPrint("ccs_goahead: Unhandled subtype %d\n" _C_ msg.SubType);
			break;
	}
}

/**
** Parse a network menu packet in client final started state
**
** @param msg message received
*/
static void ClientParseStarted(const CInitMessage &msg)
{
	switch (msg.SubType) {

		case ICMGo: // Server's final go ..
			DebugPrint("ClientParseStarted ICMGo !!!!!\n");
			NetConnectRunning = 0; // End the menu..
			break;

		default:
			DebugPrint("ccs_started: Unhandled subtype %d\n" _C_ msg.SubType);
			break;
	}
}

/**
** Parse a network menu AreYouThere keepalive packet and reply IAmHere.
**
** @param msg message received
*/
static void ClientParseAreYouThere()
{
	CInitMessage message;

	message.Type = MessageInitHello;
	message.SubType = ICMIAH;
	NetworkSendICMessage(NetworkServerIP, htons(NetworkServerPort), message);
}

/**
** Parse a network menu Bad Map reply from server.
*/
static void ClientParseBadMap()
{
	CInitMessage message;

	message.Type = MessageInitHello;
	message.SubType = ICMSeeYou;
	// Spew out 5 and trust in God that they arrive
	for (int i = 0; i < 5; ++i) {
		NetworkSendICMessage(NetworkServerIP, htons(NetworkServerPort), message);
	}
	NetConnectRunning = 0; // End the menu..
}


/**
**  Parse the initial 'Hello' message of new client that wants to join the game
**
**  @param h slot number of host msg originates from
**  @param msg message received
**  @param host  host which send the message
**  @param port  port from where the messahe nas been sent
*/
static void ServerParseHello(int h, const CInitMessage &msg, unsigned long host, int port)
{
	if (h == -1) { // it is a new client
		for (int i = 1; i < PlayerMax - 1; ++i) {
			// occupy first available slot
			if (ServerSetupState.CompOpt[i] == 0) {
				if (Hosts[i].PlyNr == 0) {
					h = i;
					break;
				}
			}
		}
		if (h != -1) {
			Hosts[h].Host = host;
			Hosts[h].Port = port;
			Hosts[h].PlyNr = h;
			Hosts[h].SetName(msg.u.Hosts[0].PlyName);
			DebugPrint("New client %d.%d.%d.%d:%d [%s]\n" _C_
					   NIPQUAD(ntohl(host)) _C_ ntohs(port) _C_ Hosts[h].PlyName);
			NetStates[h].State = ccs_connecting;
			NetStates[h].MsgCnt = 0;
		} else {
			CInitMessage message;

			message.Type = MessageInitReply;
			message.SubType = ICMGameFull; // Game is full - reject connnection
			message.MapUID = 0L;
			NetworkSendICMessage_Log(host, port, message);
			return;
		}
	}
	// this code path happens until client sends waiting (= has received this message)
	ServerSetupState.LastFrame[h] = FrameCounter;
	CInitMessage message;

	message.Type = MessageInitReply;
	message.SubType = ICMWelcome; // Acknowledge: Client is welcome
	message.u.Hosts[0].PlyNr = h; // Host array slot number
	message.u.Hosts[0].SetName(Parameters::Instance.LocalPlayerName.c_str()); // Name of server player
	for (int i = 1; i < PlayerMax - 1; ++i) { // Info about other clients
		if (i != h) {
			if (Hosts[i].PlyNr) {
				message.u.Hosts[i] = Hosts[i];
			} else {
				message.u.Hosts[i].Clear();
			}
		}
	}
	NetworkSendICMessage_Log(host, port, message);

	NetStates[h].MsgCnt++;
	if (NetStates[h].MsgCnt > 48) {
		// Detects UDP input firewalled or behind NAT firewall clients
		// If packets are missed, clients are kicked by AYT check later..
		KickDeadClient(h);
	}
}

/**
**  Parse client resync request after client user has changed menu selection
**
**  @param h slot number of host msg originates from
**  @param host  host which send the message
**  @param port  port from where the messahe nas been sent
*/
static void ServerParseResync(const int h, unsigned long host, int port)
{
	ServerSetupState.LastFrame[h] = FrameCounter;
	switch (NetStates[h].State) {
		case ccs_mapinfo:
			// a delayed ack - fall through..
		case ccs_async:
			// client has recvd welcome and is waiting for info
			NetStates[h].State = ccs_synced;
			NetStates[h].MsgCnt = 0;
			/* Fall through */
		case ccs_synced: {
			CInitMessage message;

			// this code path happens until client falls back to ICMWaiting
			// (indicating Resync has completed)
			message.Type = MessageInitReply;
			message.SubType = ICMResync;
			for (int i = 1; i < PlayerMax - 1; ++i) { // Info about other clients
				if (i != h) {
					if (Hosts[i].PlyNr) {
						message.u.Hosts[i] = Hosts[i];
					} else {
						message.u.Hosts[i].Host = 0;
						message.u.Hosts[i].Port = 0;
						message.u.Hosts[i].PlyNr = 0;
					}
				}
			}
			NetworkSendICMessage_Log(host, port, message);

			NetStates[h].MsgCnt++;
			if (NetStates[h].MsgCnt > 50) {
				// FIXME: Client sends resync, but doesn't receive our resync ack....
				;
			}
			break;
		}
		default:
			DebugPrint("Server: ICMResync: Unhandled state %d Host %d\n" _C_ NetStates[h].State _C_ h);
			break;
	}
}


/**
**  Parse client heart beat waiting message
**
**  @param h slot number of host msg originates from
**  @param host  host which send the message
**  @param port  port from where the message nas been sent
*/
static void ServerParseWaiting(const int h, unsigned long host, int port)
{
	ServerSetupState.LastFrame[h] = FrameCounter;

	switch (NetStates[h].State) {
			// client has recvd welcome and is waiting for info
		case ccs_connecting:
			NetStates[h].State = ccs_connected;
			NetStates[h].MsgCnt = 0;
			/* Fall through */
		case ccs_connected: {
			// this code path happens until client acknowledges the map
			CInitMessage message;
			message.Type = MessageInitReply;
			message.SubType = ICMMap; // Send Map info to the client
			strncpy_s(message.u.MapPath, sizeof(message.u.MapPath), NetworkMapName.c_str(), NetworkMapName.size());
			message.MapUID = Map.Info.MapUID;
			NetworkSendICMessage_Log(host, port, message);

			NetStates[h].MsgCnt++;
			if (NetStates[h].MsgCnt > 50) {
				// FIXME: Client sends waiting, but doesn't receive our map....
			}
			break;
		}
		case ccs_mapinfo:
			NetStates[h].State = ccs_synced;
			NetStates[h].MsgCnt = 0;
			for (int i = 1; i < PlayerMax - 1; ++i) {
				if (i != h && Hosts[i].PlyNr) {
					// Notify other clients
					NetStates[i].State = ccs_async;
				}
			}
			/* Fall through */
		case ccs_synced:
			// the wanted state - do nothing.. until start...
			NetStates[h].MsgCnt = 0;
			break;

		case ccs_async: {
			// Server User has changed menu selection. This state is set by MENU code
			// OR we have received a new client/other client has changed data

			// this code path happens until client acknoledges the state change
			// by sending ICMResync
			CInitMessage message;
			message.Type = MessageInitReply;
			message.SubType = ICMState; // Send new state info to the client
			message.u.State = ServerSetupState;
			message.MapUID = Map.Info.MapUID;
			NetworkSendICMessage_Log(host, port, message);

			NetStates[h].MsgCnt++;
			if (NetStates[h].MsgCnt > 50) {
				// FIXME: Client sends waiting, but doesn't receive our state info....
			}
			break;
		}
		default:
			DebugPrint("Server: ICMWaiting: Unhandled state %d Host %d\n" _C_ NetStates[h].State _C_ h);
			break;
	}
}

/**
**  Parse client map info acknoledge message
**
**  @param h slot number of host msg originates from
**  @param host  host which send the message
**  @param port  port from where the message nas been sent
*/
static void ServerParseMap(const int h, unsigned long host, int port)
{
	ServerSetupState.LastFrame[h] = FrameCounter;
	switch (NetStates[h].State) {
			// client has recvd map info waiting for state info
		case ccs_connected:
			NetStates[h].State = ccs_mapinfo;
			NetStates[h].MsgCnt = 0;
			/* Fall through */
		case ccs_mapinfo: {
			// this code path happens until client acknoledges the state info
			// by falling back to ICMWaiting with prev. State synced
			CInitMessage message;
			message.Type = MessageInitReply;
			message.SubType = ICMState; // Send State info to the client
			message.u.State = ServerSetupState;
			message.MapUID = Map.Info.MapUID;
			NetworkSendICMessage_Log(host, port, message);

			NetStates[h].MsgCnt++;
			if (NetStates[h].MsgCnt > 50) {
				// FIXME: Client sends mapinfo, but doesn't receive our state info....
				;
			}
			break;
		}
		default:
			DebugPrint("Server: ICMMap: Unhandled state %d Host %d\n" _C_ NetStates[h].State _C_ h);
			break;
	}
}

/**
**  Parse locate state change notifiction or initial state info request of client
**
**  @param h slot number of host msg originates from
**  @param msg message received
**  @param host  host which send the message
**  @param port  port from where the message nas been sent
*/
static void ServerParseState(const int h, const CInitMessage &msg, unsigned long host, int port)
{
	ServerSetupState.LastFrame[h] = FrameCounter;
	switch (NetStates[h].State) {
		case ccs_mapinfo:
			// User State Change right after connect - should not happen, but..
			/* Fall through */
		case ccs_synced:
			// Default case: Client is in sync with us, but notes a local change
			// NetStates[h].State = ccs_async;
			NetStates[h].MsgCnt = 0;
			// Use information supplied by the client:
			ServerSetupState.Ready[h] = msg.u.State.Ready[h];
			ServerSetupState.Race[h] = msg.u.State.Race[h];
			// Add additional info usage here!

			// Resync other clients (and us..)
			for (int i = 1; i < PlayerMax - 1; ++i) {
				if (Hosts[i].PlyNr) {
					NetStates[i].State = ccs_async;
				}
			}
			/* Fall through */
		case ccs_async: {
			// this code path happens until client acknoledges the state change reply
			// by sending ICMResync
			CInitMessage message;

			message.Type = MessageInitReply;
			message.SubType = ICMState; // Send new state info to the client
			message.u.State = ServerSetupState;
			message.MapUID = Map.Info.MapUID;
			NetworkSendICMessage_Log(host, port, message);

			NetStates[h].MsgCnt++;
			if (NetStates[h].MsgCnt > 50) {
				// FIXME: Client sends State, but doesn't receive our state info....
				;
			}
			break;
		}
		default:
			DebugPrint("Server: ICMState: Unhandled state %d Host %d\n" _C_ NetStates[h].State _C_ h);
			break;
	}
}

/**
**  Parse the disconnect request of a client by sending out good bye
**
**  @param h slot number of host msg originates from
**  @param host  host which send the message
**  @param port  port from where the message nas been sent
*/
static void ServerParseGoodBye(const int h, unsigned long host, int port)
{
	ServerSetupState.LastFrame[h] = FrameCounter;
	switch (NetStates[h].State) {
		default:
			// We can enter here from _ANY_ state!
			NetStates[h].MsgCnt = 0;
			NetStates[h].State = ccs_detaching;
			/* Fall through */
		case ccs_detaching: {
			// this code path happens until client acknoledges the GoodBye
			// by sending ICMSeeYou;
			CInitMessage message;
			message.Type = MessageInitReply;
			message.SubType = ICMGoodBye;
			NetworkSendICMessage_Log(host, port, message);

			NetStates[h].MsgCnt++;
			if (NetStates[h].MsgCnt > 10) {
				// FIXME: Client sends GoodBye, but doesn't receive our GoodBye....
			}
			break;
		}
	}
}

/**
** Parse the final see you msg of a disconnecting client
**
** @param h slot number of host msg originates from
*/
static void ServerParseSeeYou(const int h)
{
	switch (NetStates[h].State) {
		case ccs_detaching:
			KickDeadClient(h);
			break;

		default:
			DebugPrint("Server: ICMSeeYou: Unhandled state %d Host %d\n" _C_
					   NetStates[h].State _C_ h);
			break;
	}
}

/**
** Parse the 'I am Here' reply to the servers' 'Are you there' msg
**
** @param h slot number of host msg originates from
*/
static void ServerParseIAmHere(const int h)
{
	// client found us again - update timestamp
	ServerSetupState.LastFrame[h] = FrameCounter;
}

/**
**  Check if the Stratagus version and Network Protocol match
**
**  @param msg message received
**  @param host  host which send the message
**  @param port  port from where the message nas been sent
**
**  @return 0 if the versions match, -1 otherwise
*/
static int CheckVersions(const CInitMessage &msg, unsigned long host, int port)
{
	if (msg.Stratagus != StratagusVersion) {
		fprintf(stderr, "Incompatible Stratagus version "
				"%d <-> %d\n"
				"from %d.%d.%d.%d:%d\n",
				msg.Stratagus, StratagusVersion,
				NIPQUAD(ntohl(host)), ntohs(port));

		CInitMessage message;
		message.Type = MessageInitReply;
		message.SubType = ICMEngineMismatch; // Stratagus engine version doesn't match
		NetworkSendICMessage_Log(host, port, message);
		return -1;
	}

	if (msg.Version != NetworkProtocolVersion) {
		fprintf(stderr, "Incompatible network protocol version "
				NetworkProtocolFormatString " <-> "
				NetworkProtocolFormatString "\n"
				"from %d.%d.%d.%d:%d\n",
				NetworkProtocolFormatArgs(msg.Version),
				NetworkProtocolFormatArgs(NetworkProtocolVersion),
				NIPQUAD(ntohl(host)), ntohs(port));

		CInitMessage message;
		message.Type = MessageInitReply;
		message.SubType = ICMProtocolMismatch; // Network protocol version doesn't match
		NetworkSendICMessage_Log(host, port, message);
		return -1;
	}
	return 0;
}

static void NetworkParseMenuPacket_Client(const CInitMessage &msg, unsigned long host, int port)
{
	if (msg.Type == MessageInitReply) {
		if (msg.SubType == ICMServerQuit) { // Server user canceled, should work in all states
			NetLocalState = ccs_serverquits;
			NetConnectRunning = 0; // End the menu..
			// No ack here - Server will spew out a few Quit msgs, which has to be enough
			return;
		}
		if (msg.SubType == ICMAYT) { // Server is checking for our presence
			ClientParseAreYouThere();
			return;
		}
		switch (NetLocalState) {
			case ccs_disconnected:
#ifdef DEBUG
				ClientParseDisconnected(msg);
#endif
				break;

			case ccs_detaching:
				ClientParseDetaching(msg);
				break;

			case ccs_connecting:
				ClientParseConnecting(msg, host, port);
				break;

			case ccs_connected:
				ClientParseConnected(msg);
				break;

			case ccs_mapinfo:
				ClientParseMapInfo(msg);
				break;

			case ccs_changed:
			case ccs_synced:
				ClientParseSynced(msg, host, port);
				break;

			case ccs_async:
				ClientParseAsync(msg);
				break;

			case ccs_goahead:
				ClientParseGoAhead(msg);
				break;

			case ccs_badmap:
				ClientParseBadMap();
				break;

			case ccs_started:
				ClientParseStarted(msg);
				break;

			default:
				DebugPrint("Client: Unhandled state %d\n" _C_ NetLocalState);
				break;
		}
	}
}



static void NetworkParseMenuPacket_Server(const CInitMessage &msg, unsigned long host, int port)
{
	if (CheckVersions(msg, host, port)) {
		return;
	}
	const int index = FindHostIndexBy(host, port);

	if (index == -1) {
		if (msg.SubType == ICMHello) {
			// Special case: a new client has arrived
			ServerParseHello(-1, msg, host, port);
		}
		return;
	}

	switch (msg.SubType) {
		case ICMHello: // a new client has arrived
			ServerParseHello(index, msg, host, port);
			break;

		case ICMResync:
			ServerParseResync(index, host, port);
			break;

		case ICMWaiting:
			ServerParseWaiting(index, host, port);
			break;

		case ICMMap:
			ServerParseMap(index, host, port);
			break;

		case ICMState:
			ServerParseState(index, msg, host, port);
			break;

		case ICMMapUidMismatch:
		case ICMGoodBye:
			ServerParseGoodBye(index, host, port);
			break;

		case ICMSeeYou:
			ServerParseSeeYou(index);
			break;

		case ICMIAH:
			ServerParseIAmHere(index);
			break;

		default:
			DebugPrint("Server: Unhandled subtype %d from host %d\n" _C_ msg.SubType _C_ index);
			break;
	}
}

/**
**  Parse a Network menu packet.
**
**  @param msg message received
**  @param host  host which send the message
**  @param port  port from where the message nas been sent
*/
static void NetworkParseMenuPacket(const CInitMessage &msg, unsigned long host, int port)
{
	DebugPrint("Received %s Init Message %d:%d from %d.%d.%d.%d:%d (%ld)\n" _C_
			   icmsgsubtypenames[msg.SubType] _C_ msg.Type _C_ msg.SubType _C_ NIPQUAD(ntohl(host)) _C_
			   ntohs(port) _C_ FrameCounter);

	if (NetConnectRunning == 2) { // client
		NetworkParseMenuPacket_Client(msg, host, port);
	} else if (NetConnectRunning == 1) { // server
		NetworkParseMenuPacket_Server(msg, host, port);
	}
}

/**
**  Parse a setup event. (Command type <= MessageInitEvent)
**
**  @param buf Packet received
**  @param size size of the received packet.
**  @param host  host which send the message
**  @param port  port from where the message nas been sent
**
**  @return 1 if packet is an InitConfig message, 0 otherwise
*/
int NetworkParseSetupEvent(const unsigned char *buf, int size, unsigned long host, int port)
{
	Assert(NetConnectRunning != 0);

	if (size != (int)CInitMessage::Size()) {
		// FIXME: could be a bad packet
		if (NetConnectRunning == 2 && NetLocalState == ccs_started) {
			// Client has acked ready to start and receives first real network packet.
			// This indicates that we missed the 'Go' in started state and the game
			// has been started by the server, so do the same for the client.
			NetConnectRunning = 0; // End the menu..
		}
		return 0;
	}
	CInitMessage msg;

	msg.Deserialize(buf);
	if (msg.Type > MessageInitConfig) {
		if (NetConnectRunning == 2 && NetLocalState == ccs_started) {
			// Client has acked ready to start and receives first real network packet.
			// This indicates that we missed the 'Go' in started state and the game
			// has been started by the server, so do the same for the client.
			NetConnectRunning = 0; // End the menu..
		}
		return 0;
	}

	NetworkParseMenuPacket(msg, host, port);
	return 1;
}

/**
**  Removes Randomization of Player position in Multiplayer mode
**
**  @param l  Lua state.
*/
static int CclNoRandomPlacementMultiplayer(lua_State *l)
{
	LuaCheckArgs(l, 0);
	NoRandomPlacementMultiplayer = 1;
	return 0;
}

void NetworkCclRegister()
{
	lua_register(Lua, "NoRandomPlacementMultiplayer", CclNoRandomPlacementMultiplayer);
	lua_register(Lua, "SetMetaServer", CclSetMetaServer);
}


//@}
