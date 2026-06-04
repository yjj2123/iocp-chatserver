#pragma once
#include <Windows.h>

#define MAX_NICKNAME_LEN  32
#define MAX_ACCOUNT_LEN   32
#define MAX_PASSWORD_LEN  64
#define MAX_CHAT_LEN      256
#define MAX_ROOM_COUNT    5
#define INVALID_ROOM_NO   0xffff

enum PacketType : WORD
{
    
    CS_LOGIN_REQ        = 1,
    CS_ROOM_ENTER_REQ   = 2,
    CS_ROOM_LEAVE_REQ   = 3,
    CS_CHAT_REQ         = 4,
    CS_SIGNUP_REQ       = 5,

    SC_LOGIN_RES        = 101,
    SC_ROOM_ENTER_RES   = 102,
    SC_CHAT_RES         = 103,
    SC_USER_JOIN        = 104,
    SC_USER_LEAVE       = 105,
    SC_SIGNUP_RES       = 106
};

enum ResultCode : BYTE
{
    RESULT_SUCCESS              = 0,
    RESULT_FAIL                 = 1,
    RESULT_INVALID_NICKNAME     = 2,
    RESULT_INVALID_ROOM         = 3,
    RESULT_ALREADY_IN_ROOM      = 4,
    RESULT_NOT_IN_ROOM          = 5,

    RESULT_INVALID_ACCOUNT      = 6,   
    RESULT_INVALID_PASSWORD     = 7,   
    RESULT_DUPLICATE_ACCOUNT    = 8,   
    RESULT_DUPLICATE_NICKNAME   = 9    
};

inline ResultCode MapLoginResult(int spResult)
{
    switch (spResult)
    {
    case 0: return RESULT_SUCCESS;
    case 1: return RESULT_INVALID_ACCOUNT;
    case 2: return RESULT_INVALID_PASSWORD;
    default: return RESULT_FAIL;
    }
}

inline ResultCode MapSignUpResult(int spResult)
{
    switch (spResult)
    {
    case 0: return RESULT_SUCCESS;
    case 1: return RESULT_DUPLICATE_ACCOUNT;
    case 2: return RESULT_DUPLICATE_NICKNAME;
    default: return RESULT_FAIL;
    }
}
