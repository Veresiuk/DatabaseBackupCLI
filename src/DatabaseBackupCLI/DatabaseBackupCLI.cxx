#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>

#include <odb/exception.hxx>
#include <odb/pgsql/database.hxx>
#include <odb/transaction.hxx>

#include "BackupJobRepository.hxx"
#include "BackupService.hxx"
#include "DatabaseConnection.hxx"

namespace {

void print_usage() {
    
    std::cout
        << "DatabaseBackupCLI - database backup utility\n\n"
        << "Usage:\n"
        << "  DatabaseBackupCLI --help\n"
        << "  DatabaseBackupCLI check-db\n"
        << "  DatabaseBackupCLI list-jobs\n"
        << "  DatabaseBackupCLI backup [job-id]\n"
        << "  DatabaseBackupCLI backup-all\n"
        << "  DatabaseBackupCLI restore <database-type> "
           "<relative-dump-path> <target-database>\n"
        << "  DatabaseBackupCLI history [job-id]\n\n"
        << "  DatabaseBackupCLI backup-cluster <full|incremental>\n"
        << "  DatabaseBackupCLI combine-cluster <backup-id>\n"
        << "  DatabaseBackupCLI prune-cluster [keep-full-count] [--apply]\n"
        << "  DatabaseBackupCLI test-cluster-restore <backup-id>\n"
        << "Commands:\n"
        << "  --help    Show this help message\n"
        << "  check-db  Test the PostgreSQL control database connection\n"
        << "  list-jobs List backup jobs\n"
        << "  backup    Run the selected backup job\n"
        << "  backup-all Run all enabled backup jobs\n"
        << "  restore   Restore a dump into a target database\n"
        << "  history   Show backup history\n\n"
        << "Supported backup types and file extensions:\n"
        << "  PostgreSQL  .dump\n"
        << "  MySQL       .sql.gz\n"
        << "  MongoDB     .archive.gz\n\n"
        << "Restore example:\n"
        << "  DatabaseBackupCLI restore PostgreSQL "
           "postgres/backup.dump restore_test\n\n"
        << "Connection settings for the PostgreSQL control database:\n"
        << "  DB_HOST      Server address (default: 127.0.0.1)\n"
        << "  DB_PORT      Server port (default: 5432)\n"
        << "  DB_NAME      Database name\n"
        << "  DB_USER      Username\n"
        << "  DB_PASSWORD  User password\n"
        << "For MySQL and MongoDB backup jobs, set these variables in the "
           "environment where the CLI runs:\n"
        << "  MYSQL_BACKUP_DATABASE\n"
        << "  MONGO_BACKUP_DATABASE\n"
        << "Backup files are stored under D:/DatabaseBackups.\n";
}

bool parse_job_id(const char* text, long long& job_id) {

    try {

        const std::string value(text);
        std::size_t parsed_chars = 0;

        job_id = std::stoll(value, &parsed_chars);

        return parsed_chars == value.size() && job_id > 0;
    }

    catch (const std::exception&) {

        return false;
    }
}

bool valid_arguments(int argc, const std::string& command) {

    if (command == "check-db" || command == "list-jobs") {

        return argc == 2;
    }

    if (command == "backup") {

        return argc == 2 || argc == 3;
    }

    if (command == "restore") {

        return argc == 5;
    }

    if (command == "history") {

        return argc == 2 || argc == 3;
    }

    if (command == "backup-all") {

        return argc == 2;
    }

    if (command == "backup-cluster" || command == "combine-cluster") {

        return argc == 3;

    }

    if (command == "prune-cluster") {

        return argc >= 2 && argc <=4;
    }

    if (command == "test-cluster-restore") {

        return argc == 3;
    }

    return false;
}

}

int main(int argc, char* argv[]) {

    if (argc == 2 && std::string(argv[1]) == "--help") {

        print_usage();
        return 0;
    }

    if (argc < 2) {

        std::cerr << "Missing command.\n\n";
        print_usage();
        return 1;
    }

    const std::string command(argv[1]);

    if (!valid_arguments(argc, command)) {

        std::cerr << "Unknown command or invalid arguments.\n\n";
        print_usage();
        return 1;
    }

    long long job_id = (command == "history") ? 0 : 1;

    if ((command == "backup" || command == "history") && argc == 3) {

        if (!parse_job_id(argv[2], job_id)) {

            std::cerr << "Job ID must be a positive number.\n";
            return 1;
        }
    }

    if (command == "restore") {

        const std::string database_type(argv[2]);

    if (database_type != "PostgreSQL" &&
        database_type != "MySQL" &&
        database_type != "MongoDB") {

        std::cerr << "Unsupported database type. Use PostgreSQL, MySQL, or MongoDB.\n";
        return 1;
        }

    if (!valid_backup_filename(argv[3])) {

        std::cerr
            << "Invalid backup path. Provide a path relative to "
            << "D:/DatabaseBackups, such as postgres/backup.dump.\n";
        return 1;
        }

    if (!valid_database_name(argv[4])) {

        std::cerr
            << "Invalid target database name. Use letters, digits, "
            << "and underscores only.\n";
        return 1;
        }

    return restore_backup(argv[2], argv[3], argv[4]);
    }

    if (command == "backup-cluster") {

        return create_postgres_cluster_backup(argv[2]);
    }

    if (command == "combine-cluster") {

        return combine_postgres_cluster_backup(argv[2]);
    }

    if (command == "prune-cluster") {

        long long keep_count = 4;
        bool apply = false;

        if (argc >= 3 && !parse_job_id(argv[2], keep_count)) {

            std::cerr << "Keep count must be a positive number.\n";
            return 1;
        }

        if (keep_count > 999) {

            std::cerr << "Keep count cannot exceed 999.\n";
            return 1;
        }

        if (argc == 4) {
            
            if (std::string(argv[3]) != "--apply") {

                std::cerr << "The optional fourth argument must be --apply.\n";
                return 1;
            }

            apply = true;
        }

        return prune_postgres_cluster_backups(static_cast<unsigned int>(keep_count), apply);
    }

    if (command == "test-cluster-restore") {

        return test_postgres_cluster_restore(argv[2]);
    }

    try
    {
        std::unique_ptr<odb::pgsql::database> db = create_database_connection();

        if (command == "check-db"){

            odb::transaction transaction(db->begin());
            transaction.commit();

            std::cout << "PostgreSQL connection successful.\n";
            return 0;
        }

        if (command == "list-jobs") {

            list_jobs(*db);
            return 0;
        }

        if (command == "history") {

            return list_backup_history(job_id);
        }

        if (command == "backup") {

            return create_backup(*db, job_id);
        }

        if (command == "backup-all") {

            return create_all_backups(*db);
        }

    }
    catch (const odb::exception& e)
    {
        std::cerr << "Database error: " << e.what() << '\n';
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << '\n';
    }

    return 1;
}