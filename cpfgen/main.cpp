#include <cpflib>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {
   [[nodiscard]] int print_usage() {
	  std::cerr << "Usage: cpfgen <grammar-file> [output-directory] [--namespace <value>] [--depfile <path>]" << std::endl;
	  return 1;
   }

   void write_text_file(const std::filesystem::path& path, const std::string& content) {
	  std::ofstream stream{path};
	  if (!stream) {
		 throw std::runtime_error{"Unable to write output file '" + path.string() + "'"};
	  }

	  stream << content;
	}

	[[nodiscard]] std::string escape_depfile_path(const std::filesystem::path& path) {
	   auto text = path.string();
	   std::string escaped;
	   escaped.reserve(text.size() * 2);
	   for (char ch : text) {
		  switch (ch) {
			 case ' ': escaped += "\\ "; break;
			 case '#': escaped += "\\#"; break;
			 case '$': escaped += "$$"; break;
			 default: escaped += ch; break;
		  }
	   }
	   return escaped;
	}

	void write_depfile(
	   const std::filesystem::path& path,
	   const std::filesystem::path& generated_header,
	   const std::filesystem::path& generated_source,
	   const std::vector<std::filesystem::path>& dependencies) {
	   std::ofstream stream{path};
	   if (!stream) {
		  throw std::runtime_error{"Unable to write depfile '" + path.string() + "'"};
	   }

	   stream << escape_depfile_path(generated_header) << ' ' << escape_depfile_path(generated_source) << ':';
	   for (const auto& dependency : dependencies) {
		  stream << ' ' << escape_depfile_path(dependency);
	   }
	   stream << '\n';
	}
} // namespace

int main(int argc, char** argv) {
   try {
	  if (argc < 2) {
		 return print_usage();
	  }

	  auto grammar_path = std::filesystem::path{argv[1]};
	  std::filesystem::path output_directory;
	  std::filesystem::path depfile_path;
	  std::string code_namespace;

	  auto index = 2;
	  if (index < argc && std::string_view{argv[index]}.rfind("--", 0) != 0) {
		 output_directory = std::filesystem::path{argv[index]};
		 ++index;
	  }

	  while (index < argc) {
		 auto option = std::string_view{argv[index++]};
		 if (option == "--depfile") {
			 if (index >= argc) {
				return print_usage();
			 }
			 depfile_path = argv[index++];
		 } else if (option == "--namespace") {
			 if (index >= argc) {
				return print_usage();
			 }
			 code_namespace = argv[index++];
		 } else {
			 return print_usage();
		 }
	  }

	  if (output_directory.empty()) {
		 output_directory = grammar_path.parent_path();
	  }

	  auto base_name = grammar_path.stem().string();
	  auto loaded = cpf::load_grammar_file(grammar_path);
	  auto generated = cpf::generate_code(loaded.parsed_grammar, base_name, code_namespace);

	  std::filesystem::create_directories(output_directory);
	  auto header_path = output_directory / (base_name + ".h");
	  auto source_path = output_directory / (base_name + ".cpp");
	  write_text_file(header_path, generated.header);
	  write_text_file(source_path, generated.source);
	  if (!depfile_path.empty()) {
		 write_depfile(depfile_path, header_path, source_path, loaded.dependencies);
	  }
	  return 0;
   } catch (const std::exception& exception) {
	  std::cerr << exception.what() << std::endl;
	  return 1;
   }
}