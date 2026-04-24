#include <cpflib>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
   std::string read_text_file(const std::filesystem::path& path) {
	  std::ifstream stream{path};
	  if (!stream) {
		 throw std::runtime_error{"Unable to open grammar file '" + path.string() + "'"};
	  }

	  std::ostringstream buffer;
	  buffer << stream.rdbuf();
	  return buffer.str();
   }

   void write_text_file(const std::filesystem::path& path, const std::string& content) {
	  std::ofstream stream{path};
	  if (!stream) {
		 throw std::runtime_error{"Unable to write output file '" + path.string() + "'"};
	  }

	  stream << content;
   }
} // namespace

int main(int argc, char** argv) {
   try {
	  if (argc != 2 && argc != 3) {
		 std::cerr << "Usage: cpfgen <grammar-file> [output-directory]" << std::endl;
		 return 1;
	  }

	  auto grammar_path = std::filesystem::path{argv[1]};
	  auto output_directory = argc == 3 ? std::filesystem::path{argv[2]} : grammar_path.parent_path();
	  auto base_name = grammar_path.stem().string();
	  auto grammar_text = read_text_file(grammar_path);
	  auto grammar = cpf::parse_grammar(grammar_text);
	  auto generated = cpf::generate_code(grammar, base_name);

	  std::filesystem::create_directories(output_directory);
	  write_text_file(output_directory / (base_name + ".h"), generated.header);
	  write_text_file(output_directory / (base_name + ".cpp"), generated.source);
	  return 0;
   } catch (const std::exception& exception) {
	  std::cerr << exception.what() << std::endl;
	  return 1;
   }
}