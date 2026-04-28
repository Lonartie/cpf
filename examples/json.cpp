#include "json.h"
#include <iostream>

#define CHECK_EQ(lhs, rhs) do { \
   if ((lhs) != (rhs)) { \
      std::cerr << __FILE__ ":" << __LINE__ << ": Check '" #lhs " == " #rhs "' failed: " << (lhs) << " != " << (rhs) << std::endl; \
      std::exit(1); \
   } \
} while (0)

std::string_view unescape(std::string_view str) {
   return str.substr(1, str.size() - 2); // Remove the surrounding quotes
}

int main() {
   auto document = json::document::parse(R"(
      {
         "name": "John Doe",
         "age": 30,
         "is_student": false,
         "hobbies": ["reading", "coding", "hiking"],
         "address": {
            "street": "123 Main St",
            "city": "Anytown",
            "zip": "12345"
         }
      }
   )");

   CHECK_EQ(document.success, true);
   CHECK_EQ(document.partial, false);
   CHECK_EQ(document.forest.size(), 1);

   auto& root = *document.forest.back();
   CHECK_EQ(root.object->list.size(), 5);

   CHECK_EQ(unescape(root.object->list[0]->key->text.text), "name");
   CHECK_EQ(root.object->list[0]->value->is<json::string>(), true);
   CHECK_EQ(unescape(root.object->list[0]->value->as<json::string>().text.text), "John Doe");

   CHECK_EQ(unescape(root.object->list[1]->key->text.text), "age");
   CHECK_EQ(root.object->list[1]->value->is<json::number>(), true);
   CHECK_EQ(root.object->list[1]->value->as<json::number>().text.text, "30");

   CHECK_EQ(unescape(root.object->list[2]->key->text.text), "is_student");
   CHECK_EQ(root.object->list[2]->value->is<json::boolean>(), true);
   CHECK_EQ(root.object->list[2]->value->as<json::boolean>().value.text, "false");

   CHECK_EQ(unescape(root.object->list[3]->key->text.text), "hobbies");
   CHECK_EQ(root.object->list[3]->value->is<json::array>(), true);
   CHECK_EQ(root.object->list[3]->value->as<json::array>().list.size(), 3);
   CHECK_EQ(root.object->list[3]->value->as<json::array>().list[0]->is<json::string>(), true);
   CHECK_EQ(unescape(root.object->list[3]->value->as<json::array>().list[0]->as<json::string>().text.text), "reading");
   CHECK_EQ(root.object->list[3]->value->as<json::array>().list[1]->is<json::string>(), true);
   CHECK_EQ(unescape(root.object->list[3]->value->as<json::array>().list[1]->as<json::string>().text.text), "coding");
   CHECK_EQ(root.object->list[3]->value->as<json::array>().list[2]->is<json::string>(), true);
   CHECK_EQ(unescape(root.object->list[3]->value->as<json::array>().list[2]->as<json::string>().text.text), "hiking");

   CHECK_EQ(unescape(root.object->list[4]->key->text.text), "address");
   CHECK_EQ(root.object->list[4]->value->is<json::object>(), true);
   CHECK_EQ(root.object->list[4]->value->as<json::object>().list.size(), 3);
   CHECK_EQ(unescape(root.object->list[4]->value->as<json::object>().list[0]->key->text.text), "street");
   CHECK_EQ(root.object->list[4]->value->as<json::object>().list[0]->value->is<json::string>(), true);
   CHECK_EQ(unescape(root.object->list[4]->value->as<json::object>().list[0]->value->as<json::string>().text.text), "123 Main St");
   CHECK_EQ(unescape(root.object->list[4]->value->as<json::object>().list[1]->key->text.text), "city");
   CHECK_EQ(root.object->list[4]->value->as<json::object>().list[1]->value->is<json::string>(), true);
   CHECK_EQ(unescape(root.object->list[4]->value->as<json::object>().list[1]->value->as<json::string>().text.text), "Anytown");
   CHECK_EQ(unescape(root.object->list[4]->value->as<json::object>().list[2]->key->text.text), "zip");
   CHECK_EQ(root.object->list[4]->value->as<json::object>().list[2]->value->is<json::string>(), true);
   CHECK_EQ(unescape(root.object->list[4]->value->as<json::object>().list[2]->value->as<json::string>().text.text), "12345");

   std::cout << "All checks passed!" << std::endl;
}
