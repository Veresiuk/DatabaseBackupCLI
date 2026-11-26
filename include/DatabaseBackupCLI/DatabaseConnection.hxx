#ifndef DATABASE_CONNECTION_HXX
#define DATABASE_CONNECTION_HXX

#include <memory> 

#include <odb/pgsql/database.hxx>

std::unique_ptr<odb::pgsql::database> create_database_connection();

#endif