#ifndef BACKUP_JOB_HXX
#define BACKUP_JOB_HXX
#include <string>

#include <odb/core.hxx>
#include <odb/nullable.hxx>

#pragma db object table("backup_jobs")

class BackupJob
{
public:
   #pragma db id auto
   long long id;

   std::string name;
   std::string database_type;
   std::string backup_type;

   odb::nullable<std::string> schedule;

   #pragma db type("TEXT")
   std::string destination_path;

   bool is_enabled;
};

#endif