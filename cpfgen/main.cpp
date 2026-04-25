#include <cpflib>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
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
	  if (argc != 2 && argc != 3 && argc != 5) {
		 std::cerr << "Usage: cpfgen <grammar-file> [output-directory] [--depfile <path>]" << std::endl;
		 return 1;
	  }

	  std::filesystem::path depfile_path;
	  if (argc == 5) {
		 if (std::string_view{argv[3]} != "--depfile") {
			std::cerr << "Usage: cpfgen <grammar-file> [output-directory] [--depfile <path>]" << std::endl;
			return 1;
		 }
		 depfile_path = argv[4];
	  }

	  auto grammar_path = std::filesystem::path{argv[1]};
	  auto output_directory = argc == 3 ? std::filesystem::path{argv[2]} : grammar_path.parent_path();
	  if (argc == 5) {
		 output_directory = std::filesystem::path{argv[2]};
	  }
	  auto base_name = grammar_path.stem().string();
	  auto loaded = cpf::load_grammar_file(grammar_path);
	  auto generated = cpf::generate_code(loaded.parsed_grammar, base_name);

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