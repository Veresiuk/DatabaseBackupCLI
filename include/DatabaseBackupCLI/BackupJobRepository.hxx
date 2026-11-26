#ifndef BACKUP_JOB_REPOSITORY_HXX
#define BACKUP_JOB_REPOSITORY_HXX

#include <memory>
#include <string>
#include <vector>


#include <odb/pgsql/database.hxx>

class BackupJob;

void list_jobs(odb::pgsql::database& db);

std::vector<long long> enabled_backup_job_ids (
    
    odb::pgsql::database& db
);

int list_backup_history(long long job_id = 0);

BackupJob* find_backup_job(
    odb::pgsql::database& db,
    long long job_id
);

void record_backup_history(
    odb::pgsql::database& db,
    long long job_id,
    double started_at,
    const std::string& backup_path,
    const std::string& status,
    bool has_size,
    unsigned long long size_bytes,
    const std::string& error_message
);

#endif