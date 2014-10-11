
#include<winsock2.h>
#include<stdio.h>
#include<conio.h>//getch()
#include<string.h>
#include<synchapi.h>
#pragma comment(lib, "ws2_32.lib")

#define DEBUG
#define BUF_SIZE 1024

const unsigned char IAC   = 255;
const unsigned char DONT  = 254;
const unsigned char DO	  = 253;
const unsigned char WONT  = 252;
const unsigned char WILL  = 251;
const unsigned char SB	  = 250;
const unsigned char SE    = 240;

enum STATE{STATE_DATA, STATE_IAC, STATE_OPTION, STATE_SUBOPT};
enum VERB{VERB_WILL, VERB_WONT, VERB_DO, VERB_DONT};
enum OPTION{ECHO = 1, SGA = 3, TERMTYPE = 24}; 
enum ANSI_STATE{S_DATA, S_ESC, S_ESC0};  
enum {IS = 0, SEND = 1};
char escbuf[BUF_SIZE];  

void ParseMessage(unsigned char); 
void EchoOpt(unsigned char verb); 
void SGAOpt(unsigned char verb);  
void TermOpt(unsigned char verb); 
void RecvData(unsigned char ch); 
void ParseESC(char*,char);        
void SendReply(unsigned char, unsigned char);  
void SendNoReply(unsigned char , unsigned char);
void SendTermType();       
void ansi_set_screen_attribute(int*, int); 
void ansi_set_cursor_position(int*, int);  
void ansi_clear_screen(int*, int);      
void ansi_erase_line();     
void ansi_cursor_up(int);  
void ansi_cursor_down(int);  
void ansi_cursor_backward(int);
void ansi_cursor_forward(int);  
int SendData(SOCKET sock, char* sendbuf, int sendlen);  
char* ltrim(char*); 
char* rtrim(char*);   
void getip(char*, int);
DWORD WINAPI SendProc(LPVOID lpParemeter); 
DWORD WINAPI RecvProc(LPVOID lpParemeter); 

HANDLE hstdin;
HANDLE hstdout;
SOCKET sock;
int main()
{
	WSADATA wsaData;
	SOCKADDR_IN srvaddr;
	HANDLE hThread[2];
	int ret;
	hostent remotehost,*phostent;
	char strIP[80];

	phostent = &remotehost;

	hstdin = GetStdHandle(STD_INPUT_HANDLE);
	hstdout = GetStdHandle(STD_OUTPUT_HANDLE);
	FlushConsoleInputBuffer(hstdin);//注意，初始化刷新输入缓冲区
	COORD bufsize = {100, 40};
	SetConsoleScreenBufferSize(hstdout, bufsize);

	ret = WSAStartup(MAKEWORD(2, 2), &wsaData);//套接字socket初始化必须有

	if(ret != 0)
	{
		printf("加载套接字库失败\n");
		return -1;
	}

	if(LOBYTE(wsaData.wVersion) != 2 ||
		HIBYTE(wsaData.wVersion) != 2)
	{
		printf("套接字库版本不一致\n");
		WSACleanup();//解除初始化释放Socket库所占用的系统资源
		return -1;
	}

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(sock == INVALID_SOCKET)
	{
		printf("创建套接字失败\n");
		WSACleanup();
        return -1;
	}
	getip(strIP, 80);
	memset(&srvaddr,0,sizeof(SOCKADDR_IN));
	srvaddr.sin_addr.S_un.S_addr = inet_addr(strIP);
	srvaddr.sin_port = htons(23);//htons(23);
	srvaddr.sin_family = AF_INET;

	printf("Trying to connect...");
	ret = connect(sock, (sockaddr*)&srvaddr, sizeof(SOCKADDR_IN));
	if(ret == SOCKET_ERROR)
	{
		printf("连接服务器失败\n");
		Sleep(2000);
		closesocket(sock);
		WSACleanup();
		return -1;
	}

	printf("连接服务器成功\n");
	while(1);


	DWORD dwSendId, dwRecvId;
	hThread[0] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)SendProc, (LPVOID)&sock, 0, &dwSendId);
	hThread[1] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RecvProc, (LPVOID)&sock, 0, &dwRecvId);
	
	WaitForMultipleObjects(2, hThread, false, INFINITE);


	if(WSACleanup() == SOCKET_ERROR)
	{
		printf("WSACleanup出错\n");
		return -1;
	}
	return 0;
}

void ParseMessage(unsigned char ch)
{
	static unsigned char state = STATE_DATA;
	static unsigned char verb;

	switch(state)
	{
	case STATE_DATA:
		switch(ch)
		{
		case IAC:
			state = STATE_IAC;
			break;
		default:
			RecvData(ch);
		}
		break;
	case STATE_IAC:
		switch(ch)
		{
		case IAC:
			state = STATE_DATA;
			break;
		case WILL:
			verb = VERB_WILL;
			state = STATE_OPTION;
			break;
		case WONT:
			verb = VERB_WONT;
			state = STATE_OPTION;
			break;
		case DO:
			verb = VERB_DO;
			state = STATE_OPTION;
			break;
		case DONT:
			verb = VERB_DONT;
			state = STATE_OPTION;
			break;
		case SB:
			state = STATE_SUBOPT;
			break;
		case SE:
			state = STATE_DATA;
			break;
		default:
			state = STATE_DATA;
			break;
		}
		break;
	case STATE_OPTION:
		state = STATE_DATA;
		switch(ch)
		{
		case ECHO:
			EchoOpt(verb);
			break;
		case SGA:
			SGAOpt(verb);
			break;
		case TERMTYPE:
			TermOpt(verb);
			break;
		default:
			SendNoReply(verb, ch);
			break;
		}
		break;

	case STATE_SUBOPT:
		switch(ch)
		{
		case SE:
			state = STATE_DATA;
			break;
		case TERMTYPE:
			SendTermType();
			break;
		default:
			break;
		}
		break;
	}
}

void EchoOpt(unsigned char verb)
{
	DWORD mode;
	GetConsoleMode(hstdin, &mode);
	
	//DWORD flag = mode & ENABLE_ECHO_INPUT;
	switch(verb)
	{
	case VERB_WILL:
		SetConsoleMode(hstdin, mode & ~ENABLE_ECHO_INPUT);
		SendReply(DO, ECHO);
		break;
	case VERB_WONT:
		SetConsoleMode(hstdin, mode | ENABLE_ECHO_INPUT);
		SendReply(DONT, ECHO);
		break;
	case VERB_DO:
		SendReply(WONT, ECHO);
		break;
	case VERB_DONT:
		SendReply(WONT, ECHO);
		break;
	}
}
void SGAOpt(unsigned char verb)
{
	DWORD mode;
	GetConsoleMode(hstdin, &mode);

	//DWORD flag = mode & ENABLE_LINE_INPUT;
	switch(verb)
	{
	case VERB_WILL:
		SetConsoleMode(hstdin, mode & ~ ENABLE_LINE_INPUT);
		SendReply(DO, SGA);
		break;
	case VERB_WONT:
		SetConsoleMode(hstdin, mode | ENABLE_LINE_INPUT);
		SendReply(DONT, SGA);
		break;
	case VERB_DO:
		SendReply(WILL, SGA);
		break;
	case VERB_DONT:
		SendReply(WONT, SGA);
		break;
	}
}

void TermOpt(unsigned char verb)
{
	switch(verb)
	{
	case VERB_WILL:
		SendReply(DO, TERMTYPE);
		break;
	case VERB_WONT:
		SendReply(DONT, TERMTYPE);
		break;
	case VERB_DONT:
		SendReply(WONT, TERMTYPE);
		break;
	case VERB_DO:
		SendReply(WILL, TERMTYPE);
		break;
	}
}
void RecvData(unsigned char ch)
{
	static ANSI_STATE state = S_DATA;
	static int index;

	switch(state)
	{
	case S_DATA:
		switch(ch)
		{
		case 0:
			break;
		case 27://esc
			state = S_ESC0;
			break;
		default:
			putchar(ch);
			break;
		}
		break;
	case S_ESC0:
		switch(ch)
		{
		case'[':
			state = S_ESC;
			index = 0;
			memset(escbuf, 0, BUF_SIZE);
			break;
		default:
			state = S_DATA;
			break;
		}
		break;
	case S_ESC:
		if(ch > 64)
		{
			state = S_DATA;
			ParseESC(escbuf, ch);
		}
		else
		{
			escbuf[index++] = ch;
		}
		break;
	}
}

void ParseESC(char *buffer,char ch)
{
	int param[10];
	char strnum[10];
	int i=0;
	int count=0;
	memset(strnum, 0, 10);
	while(*buffer)
	{
		if(*buffer>='0'&&*buffer<='9')
			strnum[i++] = *buffer;
		if(*buffer == ';')
		{
			strnum[i] = '\0';
			i = 0;
			param[count++] = atoi(strnum);
		}
		buffer++;
	}
	strnum[i] = '\0';
	param[count++] = atoi(strnum);
	switch(ch)
	{
	case 'm':
		ansi_set_screen_attribute(param, count);
		break;
	case 'J':
		ansi_clear_screen(param, count);
		break;
	case 'H':
		ansi_set_cursor_position(param, count);
		break;
	case 'K':
		ansi_erase_line();
		break;
	case 'A':
		ansi_cursor_up(param[0]);
		break;
	case 'B':
		ansi_cursor_down(param[0]);
		break;
	case 'C':
		ansi_cursor_forward(param[0]);
		break;
	case 'D':
		ansi_cursor_backward(param[0]);
		break;
	default:
		break;
	}
}
void SendReply(unsigned char verb,unsigned char opt)
{
	unsigned char sendbuf[3];
	sendbuf[0] = IAC;
	sendbuf[1] = verb;
	sendbuf[2] = opt;
	send(sock, (char*)sendbuf, 3, 0);
}
void SendNoReply(unsigned char verb, unsigned char opt)
{
	switch(verb)
	{
	case VERB_WILL:
		SendReply(DONT, opt);
		break;
	case VERB_WONT:
		SendReply(DONT, opt);
		break;
	case VERB_DO:
		SendReply(WONT, opt);
		break;
	case VERB_DONT:
		SendReply(WONT, opt);
		break;
	}
}
void SendTermType()
{
	unsigned char sendbuf[10];
	sendbuf[0] = IAC;
	sendbuf[1] = SB;
	sendbuf[2] = TERMTYPE;
	sendbuf[3] = IS;
	sendbuf[4] = 'A';
	sendbuf[5] = 'N';
	sendbuf[6] = 'S';
	sendbuf[7] = 'I';
	sendbuf[8] = IAC;
	sendbuf[9] = SE;
	send(sock, (char*)sendbuf, 10, 0);
}
DWORD WINAPI SendProc(LPVOID lpParemeter)
{
	SOCKET sock = *(SOCKET*)lpParemeter;
	char sendbuf[10];
	DWORD dwRead;
	INPUT_RECORD input;
	memset(sendbuf, 0, sizeof(sendbuf));
	int sendlen;
	unsigned char ch;
	DWORD mode;
	GetConsoleMode(hstdin, &mode);
	SetConsoleMode(hstdin, mode & ~ENABLE_PROCESSED_INPUT);//输入buffer接收CTRL+C

	while(1)
	{
		WaitForSingleObject(hstdin, INFINITE);
		//ReadConsoleA(hstdin, sendbuf, sizeof(sendbuf), &dwRead, NULL);
		if(!ReadConsoleInput(hstdin, &input, 1, &dwRead))
			return -1;
		if(input.EventType == KEY_EVENT && input.Event.KeyEvent.bKeyDown)
		{
			ch = input.Event.KeyEvent.uChar.AsciiChar;
			if(ch > 0)//一般的按键有ASCII码的处理
			{
				if(ch == '\r' || ch == '\n')
				{
					sendbuf[0] = '\r';
					sendbuf[1] = '\n';//行结束符\r\n
					sendlen = 2;
					SendData(sock, sendbuf, sendlen);
				}
				else
				{
					sendbuf[0] = ch;
					sendlen = 1;
					SendData(sock, sendbuf, sendlen);
				}
			}
			switch(input.Event.KeyEvent.wVirtualKeyCode)
			{//上下左右键的处理
			case VK_UP:
				strcpy(sendbuf, "\033[A");
				sendlen = 3;
				SendData(sock, sendbuf, sendlen);
				break;
			case VK_DOWN:
				strcpy(sendbuf, "\033[B");
				sendlen = 3;
				SendData(sock, sendbuf, sendlen);
				break;
			case VK_LEFT:
				strcpy(sendbuf, "\033[D");
				sendlen = 3;
				SendData(sock, sendbuf, sendlen);
				break;
			case VK_RIGHT:
				strcpy(sendbuf, "\033[C");
				sendlen = 3;
				SendData(sock, sendbuf, sendlen);
				break;
			default:
				break;
			}
			
		}
	}
	return 0;
}
int SendData(SOCKET sock, char* sendbuf, int sendlen)
{
	int ret;
	ret = send(sock, sendbuf, sendlen, 0);
	if(ret == SOCKET_ERROR)
			return -1;
	return ret;
}
DWORD WINAPI RecvProc(LPVOID lpParemeter)
{
	SOCKET sock = *(SOCKET*)lpParemeter;
	int ret;
	char recvbuf[BUF_SIZE];
	int i;
	while(1)
	{
		i=0;
		memset(recvbuf, 0, sizeof(recvbuf));
		ret = recv(sock, recvbuf, sizeof(recvbuf), 0);
		if(ret == SOCKET_ERROR || ret == 0)
			return -1;
		while(ret--)
		{
			ParseMessage(recvbuf[i]);
			i++;
		}
	}
	return 0;
}
void ansi_set_screen_attribute(int* param, int count)
{
	int i = 0;
	WORD wAttribute;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(hstdout, &csbi);
	wAttribute = csbi.wAttributes;
	if(count == 0)
		wAttribute = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	while(count--)
	{
		switch(param[i++])
		{
		case 0:
			wAttribute = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
			break;
		case 1:
			wAttribute |= FOREGROUND_INTENSITY ;
			break;
		case 30:
			wAttribute &= 0xf8;
			break;
		case 31:
			wAttribute = wAttribute &0xf8 | 0x04;
			break;
		case 32:
			wAttribute = wAttribute &0xf8 | 0x02;
			break;
		case 33:
			wAttribute = wAttribute &0xf8 | 0x06;
			break;
		case 34:
			wAttribute = wAttribute &0xf8 | 0x01;
			break;
		case 35:
			wAttribute = wAttribute &0xf8 | 0x05;
			break;
		case 36:
			wAttribute = wAttribute &0xf8 | 0x03;
			break;
		case 37:
			wAttribute = wAttribute &0xf8 | 0x07;
			break;
		case 40:
			wAttribute &= 0x8f;
			break;
		case 41:
			wAttribute = wAttribute & 0x8f | 0x40;
			break;
		case 42:
			wAttribute = wAttribute & 0x8f | 0x20;
			break;
		case 43:
			wAttribute = wAttribute & 0x8f | 0x60;
			break;
		case 44:
			wAttribute = wAttribute & 0x8f | 0x10;
			break;
		case 45:
			wAttribute = wAttribute & 0x8f | 0x50;
			break;
		case 46:
			wAttribute = wAttribute & 0x8f | 0x30;
			break;
		case 47:
			wAttribute = wAttribute & 0x8f | 0x70;
			break;
		default:
			break;
		}
	}
	SetConsoleTextAttribute(hstdout, wAttribute);
}
void ansi_clear_screen(int* param, int count)
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	COORD pos = {0, 0};
	DWORD dwWritten;
	WORD wAttribute = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	GetConsoleScreenBufferInfo(hstdout, &csbi);
	
	FillConsoleOutputAttribute(hstdout, wAttribute, csbi.dwSize.X*csbi.dwSize.Y, pos, &dwWritten);
	FillConsoleOutputCharacter(hstdout, ' ', csbi.dwSize.X*csbi.dwSize.Y,
									pos, &dwWritten); 
	SetConsoleCursorPosition(hstdout, pos);
	
}
void ansi_set_cursor_position(int* param, int count)
{
	COORD pos= {0, 0};
	if(count == 2)
	{
		pos.Y = param[0] - 1 < 0 ? 0 : param[0] - 1;
		pos.X = param[1] - 1 < 0 ? 0 : param[1] - 1;
	}
	SetConsoleCursorPosition(hstdout, pos);
}
void ansi_erase_line()
{
	DWORD dwWritten;
	WORD wAttribute = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(hstdout, &csbi);
	DWORD len = csbi.dwSize.X - csbi.dwCursorPosition.X;
	COORD pos = csbi.dwCursorPosition;
	FillConsoleOutputAttribute(hstdout, wAttribute, len, pos, &dwWritten);
	FillConsoleOutputCharacter(hstdout, ' ', len, pos, &dwWritten);
	SetConsoleCursorPosition(hstdout, pos);
}
void ansi_cursor_up(int param)
{
	COORD pos;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(hstdout, &csbi);
	pos.X = csbi.dwCursorPosition.X;
	if(param == 0)
		param = 1;
	pos.Y = csbi.dwCursorPosition.Y - param;
	if(pos.Y < 0)
		pos.Y = 0;
	SetConsoleCursorPosition(hstdout, pos);
}

void ansi_cursor_down(int param)
{
	COORD pos;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(hstdout, &csbi);
	pos.X = csbi.dwCursorPosition.X;
	if(param == 0)
		param = 1;
	pos.Y = csbi.dwCursorPosition.Y  + param;
	if(pos.Y > csbi.dwSize.Y -1)
		pos.Y = csbi.dwSize.Y -1;
	SetConsoleCursorPosition(hstdout, pos);
}

void ansi_cursor_backward(int param)
{
	COORD pos;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(hstdout, &csbi);
	pos.Y = csbi.dwCursorPosition.Y;
	if(param == 0)
		param = 1;
	pos.X = csbi.dwCursorPosition.X - param;
	if(pos.X < 0)
		pos.X = 0;
	SetConsoleCursorPosition(hstdout, pos);
}

void ansi_cursor_forward(int param)
{
	COORD pos;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(hstdout, &csbi);
	pos.Y = csbi.dwCursorPosition.Y;
	if(param == 0)
		param = 1;
	pos.X = csbi.dwCursorPosition.X + param;
	if(pos.X > csbi.dwSize.X -1)
		pos.X = csbi.dwSize.X -1;
	SetConsoleCursorPosition(hstdout, pos);
}
char* ltrim(char* input)
{
	char* pc = input;
	if(*input == '\0')
		return input;
	while(*pc)
	{
		if(*pc == ' ' || *pc == '\t')
			pc++;
		else
			break;
	}
	if(pc != input)
		input = pc;
	return pc;

}

char* rtrim(char* input)
{
	char* pc = input;
	if(*pc == '\0')
		return input;
	int len = strlen(input);
	pc = input + len - 1;
	while(pc != input)
	{
		if(*pc == ' ' || *pc == '\t')
			pc--;
		else
			break;
	}
	if(pc != input + len -1)
		*(pc + 1) = '\0';
	return input;
}
void getip(char* strip, int len)
{
	char* pfind;
	char* pstr;
	char* temp = (char*)malloc(len);
	printf("please input server ip:");
	fgets(temp, len, stdin);
	pstr = ltrim(temp);
	pstr = rtrim(pstr);
	strcpy(strip, pstr);
	free(temp);
	
}
