#include "json.hpp"
#include <vector>
#include <cstdio>
#include <fstream>
#include <memory>
#include <iostream>
#include <algorithm>

std::vector<std::string> splitString(const std::string& input, const std::string& separator) {
	std::vector<std::string> tokens;
	size_t start = 0, end = 0;

	while ((end = input.find(separator, start)) != std::string::npos) {
		tokens.push_back(input.substr(start, end - start));
		start = end + separator.length();
	}

	tokens.push_back(input.substr(start));
	return tokens;
}

std::string trimString(const std::string& str) {
	size_t start = str.find_first_not_of(" \t\n\r");
	size_t end = str.find_last_not_of(" \t\n\r");

	if (start == std::string::npos || end == std::string::npos) {
		// The string is empty or contains only whitespaces
		return "";
	}

	return str.substr(start, end - start + 1);
}

class CommandRunner {
	std::stringstream command_;

	std::string runCommand(const char* cmd) {
		// Use popen to run the command and open a pipe to its standard output
		std::shared_ptr<FILE> pipe(_popen(cmd, "r"), _pclose);
		if (!pipe) {
			throw std::runtime_error("popen() failed!");
		}

		// Read the command output from the pipe
		char buffer[128];
		std::string result = "";
		while (!feof(pipe.get())) {
			if (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
				result += buffer;
			}
		}

		return result;
	}

public:
	CommandRunner() = default;

	template<typename T>
	CommandRunner& operator<<(T value) {
		command_ << value << ' ';
		return *this;
	}

	std::string execute() {
		auto command = command_.str();

		// remove trailing space
		command.pop_back();

		command_.clear();
		command_.str("");

		return runCommand(command.c_str());
	}
};

class Database {
	std::string dbFile_;
	CommandRunner runner_;

	std::string wrapString(const std::string& str) {
		return "\"" + str + "\"";
	}

	void omitNullStrings(std::vector<std::string>& strings) {
		strings.erase(std::remove_if(strings.begin(), strings.end(), [](const std::string& str) {
			auto isNull = true;
			for (auto c : str) {
				if (c != ' ' && c != '\t' && c != '\n') {
					isNull = false;
					break;
				}
			}
			return isNull;
			}), strings.end());
	}

public:
	Database(const std::string& filePath) {
		dbFile_ = wrapString(filePath);
	}

	std::vector<std::string> getTables() {
		runner_ << "sqlite3" << dbFile_ << ".tables";

		auto tables = splitString(runner_.execute(), " ");
		omitNullStrings(tables);

		for (auto& table : tables) {
			table = trimString(table);
		}

		return tables;
	}

	std::vector<std::string> getTableColumns(const std::string& table) {
		runner_ << "sqlite3" << dbFile_ << wrapString("PRAGMA table_info(" + table + ");");
		auto lines = splitString(runner_.execute(), "\n");
		omitNullStrings(lines);

		std::vector<std::string> columns;
		for (auto& line : lines) {
			auto parts = splitString(line, "|");
			columns.push_back(parts[1]);
		}

		return columns;
	}

	std::vector<std::vector<std::string>> getTableRecords(const std::string& table) {
		runner_ << "sqlite3" << dbFile_ << wrapString("SELECT * FROM '" + table + "';");
		auto lines = splitString(runner_.execute(), "\n");

		std::vector<std::vector<std::string>> records;
		for (auto& line : lines) {
			auto record = splitString(line, "|");
			
			auto fieldAllEmpty = true;
			for (auto& field : record) {
				if (!field.empty()) {
					fieldAllEmpty = false;
					break;
				}
			}

			if (record.size() > 0 && !fieldAllEmpty) {
				records.push_back(record);
			}
		}
		return records;
	}

	void serialize(const std::string& outputPath) {
		nlohmann::json json;

		auto tables = getTables();
		for (auto& table : tables) {
			auto& tableJson = json[table];

			auto columns = getTableColumns(table);
			auto records = getTableRecords(table);

			for (auto& record : records) {
				auto& recordJson = tableJson.emplace_back();

				for (int i = 0; i < record.size(); ++i) {
					auto field = record[i];
					auto column = columns[i];

					if (field.empty()) {
						recordJson[column];
					}
					else {
						try {
							try {
								auto integer = std::stoi(field);
								recordJson[column] = integer;
							}
							catch (...) {
								auto real = std::stod(field);
								recordJson[column] = real;
							}
						}
						catch (...) {
							recordJson[column] = field;
						}
					}
				}
			}
		}

		std::ofstream file(outputPath);
		file << json.dump(4);
	}
};

int main(int argc, char* argv[]) {
	if (argc == 1) {
		std::cerr << "Provide a database file to dump" << std::endl;
		return -1;
	}

	if (argc != 2) {
		std::cerr << "Invalid utilisation : dump-db-to-json.exe path-to-db-file.db" << std::endl;
		return -1;
	}

	std::string dbFile((argv[1])), jsonPath;
	auto dotPos = dbFile.find('.');
	if (dotPos != dbFile.npos) {
		jsonPath = dbFile.substr(0, dotPos) + ".json";
	}
	else {
		jsonPath = dbFile + ".json";
	}

	Database database(dbFile);
	try {
		database.serialize(jsonPath);
	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}

	return 0;
}