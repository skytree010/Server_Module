#include "Mars_Net_Server.h"
using namespace std;
Mars_Net_Server::Mars_Net_Server()
{
	_pFreeSession = new Mars_Lockfree_Queue<st_Session*>();
	_pPacketPool = new Mars_Memory_Pool<Mars_Serial_Buffer>();
	_pSessionQueMemory = new Mars_Memory_Pool<Mars_Lockfree_Queue<Mars_Serial_Buffer*>::st_Queue_Node>();
	_pSessionStackMemory = new Mars_Memory_Pool<Mars_Lockfree_Stack<Mars_Serial_Buffer*>::st_Stack_Node>();
}

Mars_Net_Server::~Mars_Net_Server()
{
	delete _pFreeSession;
	delete _pPacketPool;
	delete _pSessionArray;
}

bool Mars_Net_Server::Start(const WCHAR *Open_IP, WORD Port, WORD Worker_Count, bool Nagle, WORD Max_Session)
{
	WSADATA wsa;
	int Result;
	WCHAR ErrorMsg[100];
	HANDLE hThread;
	SOCKADDR_IN addr;
	unsigned int ID;
	_Nagle = Nagle;
	_MaxSessionCount = Max_Session;
	Result = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (Result != 0)
	{
		OnError(2, L"WSA Startup fail");
		swprintf_s(ErrorMsg, L"WSAStartup : %d", Result);
		OnError(0, ErrorMsg);
		return false;
	}
	_sListenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (_sListenSocket == INVALID_SOCKET)
	{
		OnError(4, L"listen socket create fail");
		Result = WSAGetLastError();
		swprintf_s(ErrorMsg, L"WSAGetLastError() : %d", Result);
		OnError(0, ErrorMsg);
		return false;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(Port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	Result = bind(_sListenSocket, (SOCKADDR*)&addr, sizeof(addr));
	if (Result == SOCKET_ERROR)
	{
		OnError(5, L"bind fail");
		Result = WSAGetLastError();
		swprintf_s(ErrorMsg, L"WSAGetLastError() : %d", Result);
		OnError(0, ErrorMsg);
		return false;
	}
	_pSessionArray = new st_Session[_MaxSessionCount];
	if (_pSessionArray == NULL)
	{
		OnError(1, L"malloc fail");
		return false;
	}
	for (int i = 0; i < _MaxSessionCount; i++)
	{
		_pSessionArray[i].IoCount = 0;
		_pSessionArray[i].SendFlag = 0;
		_pSessionArray[i].ReleaseFlag = 0;
		_pSessionArray[i].sock = 0;
		_pSessionArray[i].Index = i;
		_pSessionArray[i].SendQ = new Mars_Lockfree_Queue<Mars_Serial_Buffer*>(_pSessionQueMemory);
		if (_pSessionArray[i].SendQ == NULL)
		{
			OnError(1, L"malloc fail");
			return false;
		}
		_pSessionArray[i].SendingBuf = new Mars_Lockfree_Stack<Mars_Serial_Buffer*>(_pSessionStackMemory);
		if (_pSessionArray[i].SendingBuf == NULL)
		{
			OnError(1, L"malloc fail");
			return false;
		}
		_pSessionArray[i].RecvBuf = new Mars_Circular_Queue(_DefaultQueSize);
		if (_pSessionArray[i].RecvBuf == NULL)
		{
			OnError(1, L"malloc fail");
			return false;
		}
		if (!_pFreeSession->Inqueue(&_pSessionArray[i]))
		{
			OnError(1, L"Session Inqueue fail");
			return false;
		}
	}
	_hiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (_hiocp == NULL)
	{
		OnError(3, L"IOCP create fail");
		Result = GetLastError();
		swprintf_s(ErrorMsg, L"GetLastError() : %d", Result);
		OnError(0, ErrorMsg);
		return false;
	}
	Result = listen(_sListenSocket, SOMAXCONN);
	if (Result == SOCKET_ERROR)
	{
		OnError(6, L"LISTEN function start fail");
		Result = WSAGetLastError();
		swprintf_s(ErrorMsg, L"WSAGetLasrError() : %d", Result);
		OnError(0, ErrorMsg);
		return false;
	}
	hThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, 0, &ID);
	if (hThread == NULL || hThread == (HANDLE)-1L)
	{
		OnError(7, L"Accept Thread Start fail");
		swprintf_s(ErrorMsg, L"errno : %d", errno);
		OnError(0, ErrorMsg);
		return false;
	}
	CloseHandle(hThread);
	for (int i = 0; i < Worker_Count; i++)
	{
		hThread = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, &ID);
		if (hThread == NULL || hThread == (HANDLE)-1L)
		{
			OnError(7, L"Worker Thread Start fail");
			swprintf_s(ErrorMsg, L"errno : %d", errno);
			OnError(0, ErrorMsg);
			return false;
		}
		CloseHandle(hThread);
	}
	return true;
}

unsigned WINAPI Mars_Net_Server::AcceptThread(LPVOID p)
{
	Mars_Net_Server *_this = (Mars_Net_Server*)p;
	_this->AcceptFunc();
	return 0;
}

void Mars_Net_Server::AcceptFunc()
{
	SOCKET ClientSocket;
	st_Session *Session;
	SOCKADDR_IN ClientAddr;
	WCHAR ErrorMsg[100];
	BOOL NoDelay = TRUE;
	LINGER Lin;
	int Result;
	int SendSize = 0;
	int AddrSize;
	while (1)
	{
		AddrSize = sizeof(ClientAddr);
		ClientSocket = accept(_sListenSocket, (SOCKADDR*)&ClientAddr, &AddrSize);
		if (ClientSocket == INVALID_SOCKET)
		{
			OnError(8, L"Accept function fail");
			Result = WSAGetLastError();
			swprintf_s(ErrorMsg, L"WSAGetLastError() : %d", Result);
			OnError(0, ErrorMsg);
			continue;
		}
		Lin.l_linger = 0;
		Lin.l_onoff = 1;
		if (setsockopt(ClientSocket, SOL_SOCKET, SO_LINGER, (char*)&Lin, sizeof(Lin)))
		{
			OnError(9, L"Setsockopt function fail(linger)");
			Result = WSAGetLastError();
			swprintf_s(ErrorMsg, L"WSAGetLastError() : %d", Result);
			OnError(0, ErrorMsg);
			closesocket(ClientSocket);
		}
		if (setsockopt(ClientSocket, SOL_SOCKET, SO_SNDBUF, (char*)&SendSize, sizeof(SendSize)))
		{
			OnError(9, L"Setsockopt function fail(no send buf)");
			Result = WSAGetLastError();
			swprintf_s(ErrorMsg, L"WSAGetLastError() : %d", Result);
			OnError(0, ErrorMsg);
			closesocket(ClientSocket);
			continue;
		}
		if (!_Nagle)
		{
			if (setsockopt(ClientSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&NoDelay, sizeof(NoDelay)))
			{
				OnError(9, L"Setsockopt function fail(nagle)");
				Result = WSAGetLastError();
				swprintf_s(ErrorMsg, L"WSAGetLastError() : %d", Result);
				OnError(0, ErrorMsg);
				closesocket(ClientSocket);
				continue;
			}
		}
		if (!OnConnectionRequest(ntohl(ClientAddr.sin_addr.s_addr), ntohs(ClientAddr.sin_port)))
		{
			closesocket(ClientSocket);
			continue;
		}
		if (!_pFreeSession->Dequeue(&Session))
		{
			OnError(10, L"Not enough free session");
			closesocket(ClientSocket);
			continue;
		}
		Session->sock = ClientSocket;
		Session->SessionID = _SessionIncrement | ((long long)Session->Index << 48);
		Session->IoCount = 0;
		Session->SendFlag = 0;
		InterlockedExchange(&Session->ReleaseFlag, 0);
		_SessionIncrement++;
		_AcceptCount++;
		CreateIoCompletionPort((HANDLE)ClientSocket, _hiocp, (ULONG_PTR)Session, 0);
		InterlockedIncrement(&_SessionCount);
		PostRecv(Session);
		OnClientJoin(ntohl(ClientAddr.sin_addr.s_addr), ntohs(ClientAddr.sin_port), Session->SessionID);
	}
}

void Mars_Net_Server::PostRecv(st_Session* Sesptr)
{
	int Result;
	WSABUF buf[2];
	DWORD RecvByte = 0;
	DWORD Flag = 0;
	WCHAR ErrorMsg[100];
	if (!Sesptr->RecvBuf->GetWriteBufferPtr(&(buf[0].buf)))
	{
		OnError(11, L"Cirtular Buffer function error");
		shutdown(Sesptr->sock, SD_BOTH);
	}
	if (Sesptr->RecvBuf->GetNoCutPutSize((int*)&buf[0].len))
	{
		OnError(11, L"Cirtular Buffer function error");
		shutdown(Sesptr->sock, SD_BOTH);
	}
	if (!Sesptr->RecvBuf->GetBufferPtr(&(buf[1].buf)))
	{
		OnError(11, L"Cirtular Buffer function error");
		shutdown(Sesptr->sock, SD_BOTH);
	}
	if (Sesptr->RecvBuf->GetFreeSize((int*)&buf[1].len))
	{
		OnError(11, L"Cirtular Buffer function error");
		shutdown(Sesptr->sock, SD_BOTH);
	}
	if (Sesptr->RecvBuf->GetNoCutPutSize((int*)&Result))
	{
		OnError(11, L"Cirtular Buffer function error");
		shutdown(Sesptr->sock, SD_BOTH);
	}
	buf[1].len -= Result;
	memset(&Sesptr->Recv_Overlap, 0, sizeof(Sesptr->Recv_Overlap));
	InterlockedIncrement(&(Sesptr->IoCount));
	Result = WSARecv(Sesptr->sock, buf, 2, &RecvByte, &Flag, &(Sesptr->Recv_Overlap), NULL);
	if (Result == SOCKET_ERROR)
	{
		Result = WSAGetLastError();
		if (Result != ERROR_IO_PENDING)
		{
			if (Result != WSAECONNRESET)
			{
				OnError(12, L"Recv function fail");
				swprintf_s(ErrorMsg, L"WSAGetLastError() : %d", Result);
				OnError(0, ErrorMsg);
			}
			if (InterlockedDecrement(&Sesptr->IoCount) == 0)
				ReleaseSession(Sesptr);
		}
	}
}

unsigned WINAPI Mars_Net_Server::WorkerThread(LPVOID p)
{
	Mars_Net_Server *_this = (Mars_Net_Server*)p;
	_this->WorkerFunc();
	return 0;
}

void Mars_Net_Server::WorkerFunc()
{
	BOOL Result;
	int Err;
	char *Temp;
	unsigned long long key;
	st_Session *Sesptr;
	Mars_Serial_Buffer *Packet;
	Mars_Serial_Buffer::st_PACKET_HEADER *Header;
	DWORD Transferred = 0;
	OVERLAPPED *over = NULL;
	int BufSize;
	while (1)
	{
		Result = GetQueuedCompletionStatus(_hiocp, &Transferred, (PULONG_PTR)&key, &over, INFINITE);
		OnWorkerThreadBegin();
		Sesptr = (st_Session*)key;
		if (Result == FALSE)
		{
				shutdown(Sesptr->sock, SD_BOTH);
		}
		else if (over == &Sesptr->Recv_Overlap)
		{
			if (Transferred == 0)
			{
				shutdown(Sesptr->sock, SD_BOTH);
				if (InterlockedDecrement(&Sesptr->IoCount) == 0)
					ReleaseSession(Sesptr);
				OnWorkerThreadEnd();
				continue;
			}
			Sesptr->RecvBuf->MoveWritePos(Transferred);
			while (1)
			{
				if (Sesptr->RecvBuf->GetUseSize(&BufSize))
				{
					OnError(14, L"RecvBuf GetUse Error");
					shutdown(Sesptr->sock, SD_RECEIVE);
				}
				if (BufSize < sizeof(Mars_Serial_Buffer::st_PACKET_HEADER))
					break;
				if (_pPacketPool->Alloc(&Packet))
				{
					OnError(15, L"Packet Poll Alloc Error");
					shutdown(Sesptr->sock, SD_RECEIVE);
					break;
				}
				Packet->GetHeaderptr((char**)&Header);
				if (Sesptr->RecvBuf->Peek(Header, sizeof(Mars_Serial_Buffer::st_PACKET_HEADER)) == -1)
				{
					OnError(16, L"RecvBuf Peek Error");
					shutdown(Sesptr->sock, SD_RECEIVE);
					if (Packet->Free())
						_pPacketPool->Free(Packet);
					break;
				}
				if (Header->byCode != df_PACKET_CODE || Header->shLen > df_MAX_PACKET_SIZE)
				{
					shutdown(Sesptr->sock, SD_BOTH);
					if (Packet->Free())
						_pPacketPool->Free(Packet);
					break;
				}
				Sesptr->RecvBuf->GetUseSize(&BufSize);
				if (BufSize < Header->shLen + sizeof(Mars_Serial_Buffer::st_PACKET_HEADER))
				{
					if (Packet->Free())
						_pPacketPool->Free(Packet);
					break;
				}
				if (Sesptr->RecvBuf->RemoveData(sizeof(Mars_Serial_Buffer::st_PACKET_HEADER)) == -1)
				{
					if (Packet->Free())
						_pPacketPool->Free(Packet);
					break;
				}
				Packet->GetReadPtr((void**)&Temp);
				Err = Sesptr->RecvBuf->Get(Temp, Header->shLen);
				if (Err < Header->shLen)
				{
					shutdown(Sesptr->sock, SD_BOTH);
					if (Packet->Free())
						_pPacketPool->Free(Packet);
					break;
				}
				if (Packet->MoveWritePos(Err) != Err)
				{
					if (Packet->Free())
						_pPacketPool->Free(Packet);
					break;
				}
				if (!Packet->Decode())
				{
					shutdown(Sesptr->sock, SD_BOTH);
					if (Packet->Free())
					_pPacketPool->Free(Packet);
					break;
				}
				OnRecv(Sesptr->SessionID, Packet);
				if (Packet->Free())
					_pPacketPool->Free(Packet);
			}
			PostRecv(Sesptr);
		}
		else if (over == &Sesptr->Send_Overlap)
		{
			InterlockedExchange(&Sesptr->SendFlag, 0);
			if (Transferred == 0)
			{
				shutdown(Sesptr->sock, SD_BOTH);
				if (InterlockedDecrement(&Sesptr->IoCount) == 0)
					ReleaseSession(Sesptr);
				OnWorkerThreadEnd();
				continue;
			}
			OnSend(Sesptr->SessionID, Transferred);
			while (1)
			{
				if (!Sesptr->SendingBuf->Pop(&Packet))
					break;
				if (Packet->Free())
					_pPacketPool->Free(Packet);
			}
			if (Sesptr->SendQ->GetUseSize() > 0)
				PostSend(Sesptr);
		}
		else
		{
			OnError(17, L"overlap can't find");
			OnWorkerThreadEnd();
			continue;
		}
		if (InterlockedDecrement(&Sesptr->IoCount) == 0)
			ReleaseSession(Sesptr);
		OnWorkerThreadEnd();
	}
}

void Mars_Net_Server::ReleaseSession(st_Session* Sesptr)
{
	Mars_Serial_Buffer *Buf;
	long long SesID;
	if (InterlockedCompareExchange(&Sesptr->ReleaseFlag, 1, 0) == 1)
		return;
	closesocket(Sesptr->sock);
	SesID = Sesptr->SessionID;
	Sesptr->SessionID = 0xffffffffffffffff;
	if (!Sesptr->RecvBuf->ClearBuffer())
	{
		OnError(18, L"RecvBuf Clear fail");
	}
	while (1)
	{
		if (!Sesptr->SendQ->Dequeue(&Buf))
			break;
		if (Buf->Free())
			_pPacketPool->Free(Buf);
	}
	while (1)
	{
		if (!Sesptr->SendingBuf->Pop(&Buf))
			break;
		if (Buf->Free())
			_pPacketPool->Free(Buf);
	}
	InterlockedExchange(&Sesptr->SendFlag, 0);
	InterlockedDecrement(&_SessionCount);
	_pFreeSession->Inqueue(Sesptr);
	OnClientLeave(SesID);
}

int Mars_Net_Server::GetClientCount()
{
	return (int)_SessionCount;
}

bool Mars_Net_Server::SendPacket(__int64 SessionID, Mars_Serial_Buffer *Packet)
{
	unsigned long Index;
	unsigned long Result = 0;
	Mars_Serial_Buffer *SPacket;
	Index = SessionID >> 48;
	Result = InterlockedCompareExchange(&_pSessionArray[Index].ReleaseFlag, 1, 1);
	if (Result == 1)
	{
		return false;
	}
	if (SessionID != _pSessionArray[Index].SessionID)
	{
		OnError(20, L"SessionID not matched");
		return false;
	}
	if (_pPacketPool->Alloc(&SPacket) < 0)
		return false;
	*SPacket = *Packet;
	SPacket->SetHeader();
	SPacket->Encode();
	if (!_pSessionArray[Index].SendQ->Inqueue(SPacket))
	{
		OnError(21, L"SendQ not enough space");
		if (SPacket->Free())
			_pPacketPool->Free(SPacket);
	}
	SPacket->AddRef(1);
	if (SPacket->Free())
		_pPacketPool->Free(SPacket);
	PostSend(&_pSessionArray[Index]);
	return true;
}

void Mars_Net_Server::PostSend(st_Session* Sesptr)
{
	int Result;
	int Count;
	int i;
	DWORD SendSize;
	Mars_Serial_Buffer *Packet;
	WSABUF buf[df_BUF_SIZE];
	WCHAR ErrorMsg[100];
	if(InterlockedCompareExchange(&Sesptr->SendFlag, 1, 0) == 1)
		return;
	Count = 0;
	do
	{
		if (Count == 2)
		{
			InterlockedExchange(&Sesptr->SendFlag, 0);
			return;
		}
		Count++;
	} while (!Sesptr->SendQ->GetUseSize());

	for(i = 0; i < df_BUF_SIZE; i++)
	{
		if (!Sesptr->SendQ->Dequeue(&Packet))
			break;
		Sesptr->SendingBuf->Push(Packet);
		if (Packet->GetHeaderptr(&buf[i].buf) < 0)
			break;
		buf[i].len = Packet->GetFullSize();
	}
	memset(&Sesptr->Send_Overlap, 0, sizeof(Sesptr->Send_Overlap));
	InterlockedIncrement(&Sesptr->IoCount);
	Result = WSASend(Sesptr->sock, buf, i, &SendSize, NULL, &Sesptr->Send_Overlap, NULL);
	if (Result == SOCKET_ERROR)
	{
		Result = WSAGetLastError();
		if (Result != ERROR_IO_PENDING)
		{
			if (Result != WSAECONNRESET)
			{
				OnError(22, L"Send function fail");
				swprintf_s(ErrorMsg, L"WSAGetLastError() : %d", Result);
				OnError(0, ErrorMsg);
			}
			if (InterlockedDecrement(&Sesptr->IoCount) == 0)
				ReleaseSession(Sesptr);
		}
	}
}

bool Mars_Net_Server::Disconnect(__int64 SessionID)
{
	unsigned long Index;
	Index = SessionID >> 48;
	if (SessionID == _pSessionArray[Index].SessionID)
	{
		shutdown(_pSessionArray[Index].sock, SD_BOTH);
		return true;
	}
	OnError(23, L"Can't find sessionID at Disconnect function");
	return false;
}

__int64 Mars_Net_Server::GetMemorypoolUseSize()
{
	return _pPacketPool->GetUseSize();
}

__int64 Mars_Net_Server::GetMemorypoolAllocSize()
{
	return _pPacketPool->GetAllocSize();
}

__int64 Mars_Net_Server::GetSessionQueAllocSize()
{
	return _pSessionQueMemory->GetAllocSize();
}

__int64 Mars_Net_Server::GetSessionQueUseSize()
{
	return _pSessionQueMemory->GetUseSize();
}

__int64 Mars_Net_Server::GetSessionStackAllocSize()
{
	return _pSessionStackMemory->GetAllocSize();
}

__int64 Mars_Net_Server::GetSessionStackUseSize()
{
	return _pSessionStackMemory->GetUseSize();
}