#include "BackupService.hxx"
#include "BackupJobRepository.hxx"
#include "BackupJob.hxx"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <memory>
#include <vector>

namespace {

    const std::string backup_root = "D:/DatabaseBackups";

    bool valid_identifier(const std::string& value) {

        if (value.empty()) {
            return false;
        }

        for (unsigned char character : value) {

            if (!std::isalnum(character) && character != '_') {

                return false;

            }
        }

        return true;
    }

    bool make_backup_directory_paths(
        const std::string& destination,
        std::string& host_directory,
        std::string& container_directory) {

            host_directory = destination;

            for (char& character : host_directory) {

                if (character == '\\') {

                    character = '/';
                }
            }

            const std::string backup_root_prefix = backup_root + "/";

            if (host_directory != backup_root &&
                host_directory.compare(
                    0, backup_root_prefix.size(), backup_root_prefix) != 0) {

                return false;
            }

            const std::string relative = host_directory.substr(backup_root.size());
            std::istringstream parts(relative);
            std::string part;

            while (std::getline(parts, part, '/')) {

                if (part == "." || part == "..") {

                    return false;
                }

                for (unsigned char character : part) {

                    if (!std::isalnum(character) &&
                        character != '_' &&
                        character != '-') {

                            return false;
                        }
                }
            }

            container_directory = "/backups" + relative;
            return true;
        }
}

bool valid_backup_filename(const std::string& file_name) {

    if (file_name.empty() ||
        file_name[0] == '/' ||
        file_name.find('\\') != std::string::npos ||
        file_name[file_name.size() - 1] == '/') {

        return false;
    }

    const std::size_t slash = file_name.find_last_of('/');
    const std::size_t base_name_start =
        slash == std::string::npos ? 0 : slash + 1;

    const std::string extensions[] = {
        ".dump",
        ".sql.gz",
        ".archive.gz"
    };

    bool has_valid_extension = false;

    for (const std::string& extension : extensions) {

        if (file_name.size() - base_name_start > extension.size() &&
            file_name.compare(
                file_name.size() - extension.size(),
                extension.size(),
                extension) == 0) {

            has_valid_extension = true;
            break;
                }
    }

    if (!has_valid_extension) {

        return false;
    }

    std::string component;

    for (unsigned char character : file_name) {

        if (character == '/') {
            
            if (component.empty() || component == "." || component == "..") {

                return false;
            }

            component.clear();
            continue;
            
        }

        if (!std::isalnum(character) &&
            character != '_' &&
            character != '-' &&
            character != '.') {

            return false;
    }

    component += static_cast<char>(character);
}

return !component.empty() &&
           component != "." &&
           component != "..";
}

bool valid_database_name(const std::string& name) {

    return valid_identifier(name);

}

int create_backup(odb::pgsql::database& db, long long job_id) {

    const std::unique_ptr<BackupJob> job(find_backup_job(db, job_id));

    if (!job) {

        std::cerr << "Backup job not found: " << job_id << '\n';
        return 1;
    }

    if (!job->is_enabled) {

        std::cerr << "Backup job is disabled: " << job_id << '\n';
        return 1;
    }

    if (job->backup_type != "full") {

        std::cerr << "Only full backups are currently supported.\n";
        return 1;
    }

    std::string database_name;
    std::string extension;

    if (job->database_type == "PostgreSQL") {

        const char* value = std::getenv("DB_NAME");

        if (value == nullptr || !valid_database_name(value)) {

            std::cerr << "DB_NAME is missing or invalid.\n";
            return 1;
        }

        database_name = value;
        extension = ".dump";
    }
    else if (job->database_type == "MySQL") {

        const char* value = std::getenv("MYSQL_BACKUP_DATABASE");

        if (value == nullptr || !valid_database_name(value)) {

            std::cerr
                << "Set MYSQL_BACKUP_DATABASE to a valid MySQL database name.\n";
            return 1;
        }

        database_name = value;
        extension = ".sql.gz";
    }
    else if (job->database_type == "MongoDB") {

        const char* value = std::getenv("MONGO_BACKUP_DATABASE");

        if (value == nullptr || !valid_database_name(value)) {

            std::cerr
                << "Set MONGO_BACKUP_DATABASE to a valid MongoDB database name.\n";
            return 1;
        }

        database_name = value;
        extension = ".archive.gz";
    }
    else {

        std::cerr << "Unsupported database type: "
                  << job->database_type << '\n';
        return 1;
    }

    std::string host_directory;
    std::string container_directory;

    if (!make_backup_directory_paths(
        job->destination_path,
        host_directory,
        container_directory)) {

            std::cerr << "Backup destination must be inside "
                      << backup_root << ".\n";
            return 1;
        }

    std::error_code directory_error;
    std::filesystem::create_directories(host_directory, directory_error);

    if (directory_error) {

        std::cerr 
            << "Could not create backup destination directory: "
            << directory_error.message() << '\n';
            return 1;
    }

    const std::time_t now = std::time(nullptr);
    const std::tm* local_time = std::localtime(&now);

    if (local_time == nullptr) {

        std::cerr << "Could not get the current time.\n";
        return 1;
    }

    std::ostringstream timestamp;
    timestamp << std::put_time(local_time, "%Y%m%d_%H%M%S");

    const std::string file_name = 
        "backup_job_" + std::to_string(job_id) +
        "_" + timestamp.str() + extension;

    const std::string container_path =
        container_directory + "/" + file_name;

    const std::string host_path =
        host_directory + "/" + file_name;

    const double started_at =
        std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    std::string command;

    if (job->database_type == "PostgreSQL") {

        const char* database_user = std::getenv("DB_USER");

        if (database_user == nullptr || !valid_database_name(database_user)) {

            std::cerr << "DB_USER is missing or invalid.\n";
            return 1;
        }

        command =
            "docker compose exec -T db pg_dump "
            "-U " + std::string(database_user) +
            " -d " + database_name +
            " -Fc -f " + container_path;
    }
    else if (job->database_type == "MySQL") {

        command =
            "docker compose exec -T mysql "
            "sh /scripts/mysql_backup.sh " +
            database_name + " " + container_path;
    }
    else {

        command =
            "docker compose exec -T mongo "
            "sh /scripts/mongo_backup.sh " +
            database_name + " " + container_path;
    }

    const int result = std::system(command.c_str());

    if (result != 0)
    {
        record_backup_history(
            db,
            job_id,
            started_at,
            host_path,
            "failed",
            false,
            0,
            "Database backup command exited with a non-zero status");

        std::cerr << " backup failed. Check Docker and the database service.\n";
        return 1;
    }

    std::error_code file_error;
    const std::uintmax_t file_size =
        std::filesystem::file_size(host_path, file_error);

    if (file_error) {

        record_backup_history(
            db,
            job_id,
            started_at,
            host_path,
            "failed",
            false,
            0,
            "Could not read the backup file size");

        std::cerr << "Backup file size could not be read.\n";
        return 1;
    }

    record_backup_history(
        db,
        job_id,
        started_at,
        host_path,
        "success",
        true,
        static_cast<unsigned long long>(file_size),
        "");

    std::cout << job->database_type << " backup created for job "
              << job_id << ": " << host_path
              << " (" << file_size << " bytes)\n";

    return 0;
}

int create_all_backups(odb::pgsql::database& db) {

    const std::vector<long long> job_ids = enabled_backup_job_ids(db);

    if (job_ids.empty()) {

        std::cout << "No enabled backup jobs found.\n";
        return 0;
    }

    int failed_jobs = 0;

    for (long long job_id : job_ids) {

        std::cout << "Starting backup job " << job_id << "...\n";

        if (create_backup(db, job_id) != 0) {

            ++failed_jobs;
        }
    }

    if (failed_jobs != 0) {

        std::cerr << failed_jobs << " backup job(s) failed.\n";
        return 1;
    }

    return 0;
}

int restore_backup(
    const std::string& database_type,
    const std::string& file_name,
    const std::string& target_database
) {

    if (!valid_backup_filename(file_name)) {

        std::cerr << "Invalid backup file path or extension.\n";
        return 1;
    }

    if (!valid_database_name(target_database)) {

        std::cerr << "Invalid target database name.\n";
        return 1;
    }

    const std::string container_path = "/backups/" + file_name;

    std::string command;

    if (database_type == "PostgreSQL") {

        const char* database_user = std::getenv("DB_USER");

        if (database_user == nullptr ||
            !valid_database_name(database_user)) {

            std::cerr << "DB_USER is missing or invalid.\n";
            return 1;
        }

        if (file_name.size() < 5 ||
            file_name.compare(file_name.size() - 5, 5, ".dump") != 0) {

            std::cerr << "PostgreSQL restore requires a .dump file.\n";
            return 1;
        }

        command =
            "docker compose exec -T db pg_restore "
            "--exit-on-error "
            "-U " + std::string(database_user) +
            " -d " + target_database +
            " " + container_path;
    }
    else if (database_type == "MySQL") {

        if (file_name.size() < 7 ||
            file_name.compare(file_name.size() - 7, 7, ".sql.gz") != 0) {

            std::cerr << "MySQL restore requires a .sql.gz file.\n";
            return 1;
        }

        command =
            "docker compose exec -T mysql "
            "sh /scripts/mysql_restore.sh " +
            target_database + " " + container_path;
    }
    else if (database_type == "MongoDB") {

        if (file_name.size() < 11 ||
            file_name.compare(file_name.size() - 11, 11, ".archive.gz") != 0) {

            std::cerr << "MongoDB restore requires an .archive.gz file.\n";
            return 1;
        }

        const char* source_database =
            std::getenv("MONGO_BACKUP_DATABASE");

        if (source_database == nullptr ||
            !valid_database_name(source_database)) {

            std::cerr
                << "Set MONGO_BACKUP_DATABASE to the source database name "
                << "used when creating the MongoDB backup.\n";
            return 1;
        }

        command =
            "docker compose exec -T mongo "
            "sh /scripts/mongo_restore.sh " +
            std::string(source_database) + " " +
            target_database + " " +
            container_path;
    }
    else {

        std::cerr
            << "Unsupported database type. Use PostgreSQL, MySQL, or MongoDB.\n";
        return 1;
    }

    const int result = std::system(command.c_str());

    if (result != 0) {

        std::cerr
            << "Restore failed. Check that the dump exists and the target "
            << "database is available.\n";
        return 1;
    }

    std::cout << database_type
              << " restore completed into database: "
              << target_database << '\n';
    return 0;
}

bool valid_postgres_cluster_backup_id(const std::string& backup_id) {

    const bool is_full = backup_id.compare(0, 5, "full_") == 0;
    const bool is_incremental =
        backup_id.compare(0, 12, "incremental_") == 0;

    const std::size_t prefix_size = is_full ? 5 : 12;
    const std::size_t expected_size = prefix_size + 16;

    if ((!is_full && !is_incremental) ||
        backup_id.size() != expected_size) {

        return false;
    }

    const std::size_t timestamp_start = prefix_size;

    for (std::size_t i = 0; i < 8; ++i) {
        
        if (!std::isdigit(
                static_cast<unsigned char>(backup_id[timestamp_start + i])))
        {
            return false;
        }
    }

    if (backup_id[timestamp_start + 8] != 'T') {

        return false;
    }

    for (std::size_t i = 9; i < 15; ++i) {

        if (!std::isdigit(static_cast<unsigned char>(backup_id[timestamp_start + i]))) {

            return false;
        }
    }

    return backup_id[timestamp_start + 15] == 'Z';
}

int create_postgres_cluster_backup(const std::string& mode) {

    if (mode != "full" && mode != "incremental") {

        std::cerr << "Backup mode must be full or incremental.\n";
        return 1;
    }

    const std::string command = 
        "docker compose exec -T db "
        "bash /scripts/postgres_cluster_backup.sh " + mode;

    if (std::system(command.c_str()) != 0) {

        std::cerr << "PostgreSQL cluster backup failed.\n";
        return 1;
    }

    return 0;

}

    int combine_postgres_cluster_backup(const std::string& backup_id) {

        if (!valid_postgres_cluster_backup_id(backup_id)) {

            std::cerr << "Invalid cluster backup ID.\n";
            return 1;
        }

        const std::string command = 
            "docker compose exec -T db "
            "bash /scripts/postgres_cluster_combine.sh " + backup_id;

        if (std::system (command.c_str()) != 0) {

            std::cerr << "Could not combine PostgreSQL cluster backups.\n";
            return 1;
        }

        return 0;
    }

int prune_postgres_cluster_backups(unsigned int keep_full_chains, bool apply) {

    if (keep_full_chains == 0 || keep_full_chains > 999) {

        std::cerr << "Keep count must be between 1 and 999.\n";
        return 1;
    }

    std::string command = 
        "docker compose exec -T db bash "
        "/scripts/postgres_cluster_retention.sh " +
        std::to_string(keep_full_chains);

    if (apply) {
        
        command += " --apply";
    }

    if (std::system(command.c_str()) != 0) {

        std::cerr << "PostgreSQL cluster retention command failed.\n";
        return 1;
    }

    return 0;
}

int test_postgres_cluster_restore(const std::string& backup_id) {

    if(!valid_postgres_cluster_backup_id(backup_id)) {

        std::cerr << "Invalid cluster backup ID.\n";
        return 1;
    }

    const std::string command =
        "docker compose exec -T db "
        "bash /scripts/postgres_cluster_restore_test.sh " + backup_id;

    if (std::system(command.c_str()) != 0) {

        std::cerr << "PostgreSQL cluster restore test failed.\n";
        return 1;
    }

    return 0;
}
