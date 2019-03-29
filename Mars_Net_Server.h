#pragma once
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "mswsock")
#include <WinSock2.h>
#include <Windows.h>
#include <process.h>
#include "..\RingBuffer\Mars_Circular_Queue.h"
#include "..\SerialBuffer\Mars_Serial_Buffer.h"
#include "..\DataStructure\Mars_Lockfree_Queue.h"
#include "..\DataStructure\Mars_Lockfree_Stack.h"
#include "..\DataStructure\Mars_Memory_Pool.h"
#include "..\CrashDump\Mars_Crash_Dump.h"
#include "..\SystemLog\Mars_SysLog.h"
#include "..\Profiler\Mars_Profiler.h"

#define df_MAX_PACKET_SIZE 200
#define df_BUF_SIZE 100
class Mars_Net_Server
{
private:
	struct st_Session
	{
		__int64 SessionID;
		unsigned long Index;
		unsigned long ReleaseFlag;
		unsigned long IoCount;
		unsigned long SendFlag;
		SOCKET sock;
		Mars_Lockfree_Queue<Mars_Serial_Buffer*> *SendQ;
		Mars_Lockfree_Stack<Mars_Serial_Buffer*> *SendingBuf;
		Mars_Circular_Queue *RecvBuf;
		OVERLAPPED Send_Overlap;
		OVERLAPPED Recv_Overlap;
	};

public:
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//	This functions are for check network module
	//	so if you call this function you can get result at return value.
	//	ex) if you want to get TPS(Transaction per second) you call GetSendTPS();
	//	and you can get TPS value at return value.
	//////////////////////////////////////////////////////////////////////////////////////////////////
	__int64 GetMemorypoolUseSize();
	__int64 GetMemorypoolAllocSize();
	__int64 GetSessionQueAllocSize();
	__int64 GetSessionQueUseSize();
	__int64 GetSessionStackAllocSize();
	__int64 GetSessionStackUseSize();
	DWORD GetSendTPS();
	DWORD GetRecvTPS();
	__int64 GetAcceptCount();
	int GetClientCount();

public:
	Mars_Net_Server();
	~Mars_Net_Server();

	//////////////////////////////////////////////////////////////////////////////////////////////////
	//	This functions are for control server module. 
	//	so if you want to control server module you can use this.
	//	ex) if you want to send packet to some session you call SendPacket(_Session, Packet);
	//	and worker thread send that packet to correct session.
	//////////////////////////////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////////////////////////////
	//	Function Name	:Start
	//	Return Value	:bool succese -> true false -> false
	//	Argument		:Open_IP(L"192.168.0.1"), Port(80), Worker_Count(How many worker thread work)
	//					 Nagle(if you want to turn on the nagle algorithm true), Max_Session
	//	description		:
	//////////////////////////////////////////////////////////////////////////////////////////////////
	bool Start(const WCHAR *Open_IP, WORD Port, WORD Worker_Count, bool Nagle, WORD Max_Session);
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//	Function Name	:Stop
	//	Return Value	:None
	//	Argument		:None
	//	description		:Not Used.
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//	void Stop();
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//	Function Name	:Disconnect
	//	Return Value	:bool succese -> true false -> false
	//	Argument		:SessionID()
	//	description		:If you want to disconnect some session you can use this function easily.
	//////////////////////////////////////////////////////////////////////////////////////////////////
	bool Disconnect(__int64 SessionID);	
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//	Function Name	:SendPacket
	//	Return Value	:bool succese : true false : false
	//	Argument		:Open_IP(L"192.168.0.1"), Port(80), Worker_Count(How many worker thread work)
	//					 Nagle(if you want to turn on the nagle algorithm true), Max_Session
	//	description		:
	//////////////////////////////////////////////////////////////////////////////////////////////////
	bool SendPacket(__int64 SessionID, Mars_Serial_Buffer *Packet);

	//////////////////////////////////////////////////////////////////////////
	//Accept후 접속처리 완료후 호출
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//	Function Name	:OnClientJoin
	//	Return Value	:None
	//	Argument		:IP(), Port(), SessionID()
	//	description		:
	//////////////////////////////////////////////////////////////////////////////////////////////////
	virtual void OnClientJoin(ULONG IP, WORD Port, __int64 SessionID) = 0;
	//////////////////////////////////////////////////////////////////////////
	//Disconnect후 호출
	//////////////////////////////////////////////////////////////////////////
	virtual void OnClientLeave(__int64 SessionID) = 0;
	//////////////////////////////////////////////////////////////////////////
	//Accept후 return 을 true시 접속완료처리 false시 접속거부
	//////////////////////////////////////////////////////////////////////////
	virtual bool OnConnectionRequest(ULONG IP, WORD Port) = 0;
	//////////////////////////////////////////////////////////////////////////
	//패킷 수신완료시 호출
	//////////////////////////////////////////////////////////////////////////
	virtual void OnRecv(__int64 SessionID, Mars_Serial_Buffer *Packet) = 0;
	//////////////////////////////////////////////////////////////////////////
	//패킷 송신완료시 호출
	//////////////////////////////////////////////////////////////////////////
	virtual void OnSend(__int64 SessionID, int sendsize) = 0;
	//////////////////////////////////////////////////////////////////////////
	//워커스레드 GQCS 바로 하단에서 호출
	//////////////////////////////////////////////////////////////////////////
	virtual void OnWorkerThreadBegin() = 0;
	//////////////////////////////////////////////////////////////////////////
	//워커스레드 1루프 종료 후 호출
	//////////////////////////////////////////////////////////////////////////
	virtual void OnWorkerThreadEnd() = 0;
	//////////////////////////////////////////////////////////////////////////
	//에러시 호출
	//////////////////////////////////////////////////////////////////////////
	virtual void OnError(int errorcode, const WCHAR *ErrorString) = 0;

private:
	static unsigned WINAPI AcceptThread(LPVOID p);
	static unsigned WINAPI WorkerThread(LPVOID p);
	void WorkerFunc();
	void AcceptFunc();
	void PostRecv(st_Session* Sesptr);
	void ReleaseSession(st_Session* Sesptr);
	void PostSend(st_Session* Sesptr);

private:
	Mars_Memory_Pool<Mars_Lockfree_Queue<Mars_Serial_Buffer*>::st_Queue_Node> *_pSessionQueMemory;
	Mars_Memory_Pool<Mars_Lockfree_Stack<Mars_Serial_Buffer*>::st_Stack_Node> *_pSessionStackMemory;
	st_Session *_pSessionArray;
	Mars_Lockfree_Queue<st_Session*> *_pFreeSession;
	Mars_Memory_Pool<Mars_Serial_Buffer> *_pPacketPool;
	SOCKET _sListenSocket;
	HANDLE _hiocp;
	int _MaxSessionCount;
	bool _Nagle;
	__int64 _SessionIncrement = 1;
	DWORD _SendTimeTick;
	DWORD _SaveSend = 0;
	DWORD _SendTPS = 0;
	DWORD _RecvTimeTick;
	DWORD _SaveRecv = 0;
	DWORD _RecvTPS = 0;
	
private:
	const int _DefaultQueSize = 1234;
};