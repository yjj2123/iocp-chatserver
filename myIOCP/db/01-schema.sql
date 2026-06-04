-- IOCP Chat Server schema (MSSQL Express 2019, ODBC Driver 17)
-- Run against the target database: Users table + sp_Login + sp_SignUp

CREATE TABLE Users (
    UserNo   INT NOT NULL PRIMARY KEY IDENTITY(1, 1),
    Account  NVARCHAR(32) NOT NULL UNIQUE,
    Password NVARCHAR(64) NOT NULL,
    Nickname NVARCHAR(32) NOT NULL UNIQUE,
    RegDate  DATETIME2 NOT NULL DEFAULT SYSUTCDATETIME()
);
GO

CREATE PROCEDURE sp_Login @Account NVARCHAR(32), @PassWord NVARCHAR(64)
AS BEGIN
    SET NOCOUNT ON;
    DECLARE @UserNo INT = 0;
    DECLARE @StoredPassword NVARCHAR(64) = N'';
    DECLARE @Nickname NVARCHAR(32) = N'';

    SELECT @UserNo = UserNo, @StoredPassword = Password, @Nickname = Nickname
    FROM Users WHERE Account = @Account;

    IF @UserNo = 0
    BEGIN SELECT 1 AS Result, 0 AS UserNo, N'' AS Nickname; RETURN; END
    IF @StoredPassword <> @PassWord
    BEGIN SELECT 2 AS Result, 0 AS UserNo, N'' AS Nickname; RETURN; END

    SELECT 0 AS Result, @UserNo AS UserNo, @Nickname AS Nickname;
END
GO

CREATE PROCEDURE sp_SignUp @Account NVARCHAR(32), @Password NVARCHAR(64), @Nickname NVARCHAR(32)
AS BEGIN
    SET NOCOUNT ON;
    IF EXISTS (SELECT 1 FROM Users WHERE Account = @Account)
    BEGIN SELECT 1 AS Result, 0 AS UserNo; RETURN; END
    IF EXISTS (SELECT 1 FROM Users WHERE Nickname = @Nickname)
    BEGIN SELECT 2 AS Result, 0 AS UserNo; RETURN; END

    INSERT INTO Users (Account, Password, Nickname) VALUES (@Account, @Password, @Nickname);
    SELECT 0 AS Result, SCOPE_IDENTITY() AS UserNo;
END
GO
