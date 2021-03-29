#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "mysqlpool.hpp"

using namespace std;

Mysql_conection_pool::Mysql_conection_pool()
{
	this->_CurConn = 0;
	this->_FreeConn = 0;
}

//单例创建，c++11后已经规定为原子操作，无需加锁
Mysql_conection_pool *Mysql_conection_pool::GetInstance()
{
	static Mysql_conection_pool connPool;
	return &connPool;
}

//构造初始化
void Mysql_conection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn)
{
	this->_url = url;
	this->_Port = Port;
	this->_User = User;
	this->_PassWord = PassWord;
	this->_DatabaseName = DBName;

	_lock.lock();       //涉及到链表等操作，需加锁
	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL)
		{
			cout << "Error:" << mysql_error(con);
			exit(1);
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			cout << "Error: " << mysql_error(con);
			exit(1);
		}
		_connList.push_back(con);
		++_FreeConn;
	}

    //创建信息量
	_reserve = sem(_FreeConn);

	this->_MaxConn = _FreeConn;
	
	_lock.unlock();
}


//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *Mysql_conection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == _connList.size())
		return NULL;

	_reserve.wait();    //会阻塞等待
	
	_lock.lock();

	con = _connList.front();
	_connList.pop_front();

	--_FreeConn;
	++_CurConn;

	_lock.unlock();
	return con;
}

//释放当前使用的连接
bool Mysql_conection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	_lock.lock();

	_connList.push_back(con);
	++_FreeConn;
	--_CurConn;

	_lock.unlock();

	_reserve.post();
	return true;
}

//销毁数据库连接池
void Mysql_conection_pool::DestroyPool()
{

	_lock.lock();
	if (_connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = _connList.begin(); it != _connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		_CurConn = 0;
		_FreeConn = 0;
		_connList.clear();

		_lock.unlock();
	}

	_lock.unlock();
}

//当前空闲的连接数
int Mysql_conection_pool::GetFreeConn()
{
	return this->_FreeConn;
}

//析构函数中释放
Mysql_conection_pool::~Mysql_conection_pool()
{
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, Mysql_conection_pool *connPool){
	*SQL = connPool->GetConnection();
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}