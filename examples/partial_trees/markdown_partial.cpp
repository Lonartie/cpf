#include "markdown_partial.h"
#include <iostream>
#include <string_view>
#include <type_traits>

namespace {
   auto require(bool condition, std::string_view message) -> bool {
      if (!condition) {
         std::cerr << message << std::endl;
      }
      return condition;
   }
}

int main() {
   constexpr auto input =
         "# release "
         "- [x] parser "
         "- [x docs "
         "shipped";

   cpf::parse_options options;
   options.allow_partial = true;

   auto result = partial_trees::document::parse(input, options);
   if (!require(result.success, "partial markdown input should still produce a recovered tree")) {
      return 1;
   }
   if (!require(result.partial, "partial markdown input should report partial success")) {
      return 1;
   }
   if (!require(result.status == cpf::parse_status::partial_success,
                "partial markdown input should report partial_success")) {
      return 1;
   }
   if (!require(result.error.has_value(), "partial markdown input should return a recovery diagnostic")) {
      return 1;
   }
   if (!require(result.forest.size() == 1, "partial markdown input should keep one recovered tree")) {
      return 1;
   }
   if (!require(result.forest.front().is_partial(), "returned tree should be marked as partial")) {
      return 1;
   }

   auto heading_count = std::size_t{0};
   auto task_item_count = std::size_t{0};
   auto paragraph_count = std::size_t{0};
   auto damaged_node_count = std::size_t{0};
   partial_trees::visit_recursive(*result.forest.front(), [&](const auto& node) {
      using node_t = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<node_t, partial_trees::heading>) {
         ++heading_count;
      } else if constexpr (std::is_same_v<node_t, partial_trees::task_item>) {
         ++task_item_count;
      } else if constexpr (std::is_same_v<node_t, partial_trees::paragraph>) {
         ++paragraph_count;
      }
      if (node.is_damaged()) {
         ++damaged_node_count;
      }
   });

   if (!require(heading_count == 1, "recovered markdown tree should keep the heading block")) {
      return 1;
   }
   if (!require(task_item_count == 2, "recovered markdown tree should keep both task items")) {
      return 1;
   }
   if (!require(paragraph_count == 1, "recovered markdown tree should keep the paragraph block")) {
      return 1;
   }
   if (!require(damaged_node_count >= 1, "recovered markdown tree should expose damaged nodes")) {
      return 1;
   }
   if (!require(!result.forest.front().damaged_nodes().empty(),
                "recovered markdown tree should index damaged nodes")) {
      return 1;
   }

   auto repaired = result.forest.front().try_repair_input(input);
   if (!require(repaired.has_value(), "recovered markdown tree should rebuild repaired input")) {
      return 1;
   }

   std::cout << "Recovered markdown input:\n" << *repaired << std::endl;

   auto clean_result = partial_trees::document::parse(*repaired);
   if (!require(clean_result.success, "repaired markdown input should parse successfully")) {
      return 1;
   }
   if (!require(!clean_result.partial, "repaired markdown input should no longer be partial")) {
      return 1;
   }
   if (!require(clean_result.forest.size() == 1, "repaired markdown input should produce one tree")) {
      return 1;
   }

   std::cout << "Partial tree example checks passed." << std::endl;
}

