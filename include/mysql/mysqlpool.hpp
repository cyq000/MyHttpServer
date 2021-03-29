#ifndef _MYSQLPOOL_H
#define _MYSQLPOOL_H

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.hpp"

using namespace std;

class Mysql_conection_pool
{
public:
    Mysql_conection_pool();
	~Mysql_conection_pool();
	
    MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取连接
	void DestroyPool();					 //销毁所有连接

	//单例模式
	static Mysql_conection_pool *GetInstance();

	void init(string url, string User, string PassWord, string DataBaseName, int Port,int MaxConn); 
	


private:
	int _MaxConn;  //最大连接数
	int _CurConn;  //当前已使用的连接数
	int _FreeConn; //当前空闲的连接数

private:
	locker _lock;
	list<MYSQL *> _connList; //连接池
	sem _reserve;

private:
	string _url;		  //主机地址
	string _Port;		  //数据库端口号
	string _User;		  //登陆数据库用户名
	string _PassWord;	  //登陆数据库密码
	string _DatabaseName; //使用数据库名
};


//管理连接和释放
class connectionRAII{

public:
	connectionRAII(MYSQL **con, Mysql_conection_pool *connPool);
	~connectionRAII();
	
private:
	MYSQL *conRAII;
	Mysql_conection_pool *poolRAII;
};

#endif
