#ifndef BACKUP_SERVICE_HXX
#define BACKUP_SERVICE_HXX

#include <string>

#include <odb/pgsql/database.hxx>

int create_backup(
    odb::pgsql::database& db,
    long long job_id);

int create_all_backups(odb::pgsql::database& db);

int restore_backup(
    const std::string& database_type,
    const std::string& file_name,
    const std::string& target_database
);

bool valid_backup_filename(const std::string& file_name);
bool valid_database_name(const std::string& name);

int create_postgres_cluster_backup(const std::string& mode);
int combine_postgres_cluster_backup(const std::string& backup_id);
bool valid_postgres_cluster_backup_id(const std::string& backup_id);
int prune_postgres_cluster_backups(unsigned int keep_full_chains,bool apply);
int test_postgres_cluster_restore(const std::string& backup_id);

#endif