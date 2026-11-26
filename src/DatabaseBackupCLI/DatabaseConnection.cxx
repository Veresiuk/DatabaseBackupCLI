#include "DatabaseConnection.hxx"

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {

    const char* required_env(const char* name) {

        const char* value = std::getenv(name);

        if (value == nullptr || *value == '\0') {

            throw std::runtime_error(
                std::string("Missing environment variable: ") + name);
        }

        return value;

    }

    unsigned int database_port() {

        const char* text = std::getenv("DB_PORT");

        if (text == nullptr || *text == '\0') {

            return 5432;
        }

        const std::string value(text);
        std::size_t parsed_chars = 0;
        unsigned long port = 0;

        try {

            port = std::stoul(value, &parsed_chars);

        } catch (const std::exception&){

            throw std::runtime_error("DB_PORT must be a number from 1 to 65535.");

        }

        if (parsed_chars != value.size() || port < 1 || port > 65535) {

            throw std::runtime_error("DB_PORT must be a number from 1 to 65535.");

        }

        return static_cast<unsigned int>(port);

    }
}

std::unique_ptr<odb::pgsql::database> create_database_connection() {

    const char* host = std::getenv("DB_HOST");

    if (host == nullptr || *host == '\0') {

        host = "127.0.0.1";
    }

    return std::make_unique<odb::pgsql::database> (

        required_env("DB_USER"),
        required_env("DB_PASSWORD"),
        required_env("DB_NAME"),
        host,
        database_port()

    );
}