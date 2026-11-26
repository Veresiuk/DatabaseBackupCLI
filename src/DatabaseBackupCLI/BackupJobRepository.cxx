#include "BackupJobRepository.hxx"
#include "BackupJob.hxx"
#include "BackupJob-odb.hxx"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <cctype>
#include <cstdlib>
#include <chrono>

#include <odb/result.hxx>
#include <odb/transaction.hxx>

namespace {

    std::string sql_string(const std::string& value) {

        std::string escaped;

        for (char character : value) {

            escaped += character;

            if (character == '\'') {

                escaped += '\'';
            } 
        }

        return "'" + escaped + "'";
    }

    bool valid_identifier(const char* value) {
        
    if (value == nullptr || *value == '\0') {

        return false;
    }

    for (const unsigned char character : std::string(value)) {

        if (!std::isalnum(character) && character != '_') {

            return false;
        }
    }

    return true;
}
}

void list_jobs(odb::pgsql::database& db) {

    odb::transaction transaction(db.begin());
    odb::result<BackupJob> jobs(db.query<BackupJob>());

    bool found = false;

    for (odb::result<BackupJob>::iterator i(jobs.begin());
        i != jobs.end();
        ++i) {

            const BackupJob& job(*i);
            found = true;

            std::cout 
                << "ID: " << job.id
                << " | Name: " << job.name
                << " | Database: " << job.database_type
                << " | Type: " << job.backup_type
                << " | Schedule: ";

            if (job.schedule) {

                std::cout << *job.schedule;

            }else {

                std::cout << "-";
            }

            std::cout 
                << " | Destination: " << job.destination_path
                << " | Enabled: " << (job.is_enabled ? "yes" : "no")
                << '\n';
        }

        if (!found) {
            std::cout << "No backup jobs found.\n";
        }

        transaction.commit();
}

std::vector<long long> enabled_backup_job_ids(
    odb::pgsql::database& db
) {
    odb::transaction transaction(db.begin());

    odb::result<BackupJob> jobs(
        db.query<BackupJob>(
            odb::query<BackupJob>::is_enabled == true
        )
    );

    std::vector<long long> ids;

    for (odb::result<BackupJob>::iterator i(jobs.begin());
         i != jobs.end();
         ++i) {
            
        const BackupJob& job(*i);
        ids.push_back(job.id);
    }

    transaction.commit();
    return ids;
}

int list_backup_history(long long job_id) {

    const char* database_user = std::getenv("DB_USER");
    const char* database_name = std::getenv("DB_NAME");

    if (!valid_identifier(database_user) ||
        !valid_identifier(database_name)) {

        std::cerr
            << "DB_USER and DB_NAME must contain only letters, digits, "
            << "and underscores.\n";
        return 1;
    }

    std::string sql =
        "SELECT id, job_id, status, started_at, finished_at, "
        "backup_path, size_bytes, error_message "
        "FROM backup_history";

    if (job_id > 0) {

        sql += " WHERE job_id = " + std::to_string(job_id);
    }

    sql += " ORDER BY id DESC LIMIT 20;";

    const std::string command =
        "docker compose exec -T db psql -P pager=off "
        "-U " + std::string(database_user) +
        " -d " + std::string(database_name) +
        " -c \"" + sql + "\"";

    const int result = std::system(command.c_str());

    if (result != 0) {

        std::cerr << "Could not read backup history.\n";
        return 1;
    }

    return 0;
}

    BackupJob* find_backup_job(
    odb::pgsql::database& db, long long job_id) {

        odb::transaction transaction(db.begin());

        BackupJob* job = db.find<BackupJob>(job_id);

        transaction.commit();
        return job;
}

void record_backup_history(
    odb::pgsql::database& db,
    long long job_id,
    double started_at,
    const std::string& backup_path,
    const std::string& status,
    bool has_size,
    unsigned long long size_bytes,
    const std::string& error_message
) {
    std::ostringstream sql;

    const double finished_at = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sql << "INSERT INTO backup_history "
        << "(job_id, started_at, finished_at, status, "
        << "backup_path, size_bytes, error_message) "
        << "VALUES ("
        << job_id
        << ", to_timestamp("
        << std::fixed << std::setprecision(6) << started_at
        << "), to_timestamp("
        << std::fixed << std::setprecision(6) << finished_at
        << "), "        
        << sql_string(status)
        << ", "
        << sql_string(backup_path)
        << ", ";

    if (has_size) {

        sql << size_bytes;
    } else {
        sql << "NULL";
    }

    sql << ", ";

    if (error_message.empty()) {

        sql << "NULL";

    } else {
        sql << sql_string(error_message);
    }

    sql << ")";

    odb::transaction transaction(db.begin());
    db.execute(sql.str());
    transaction.commit();
}