# TODO's

* String format: Allow single quotes as well as double quotes for string literals in the grammar
* Move everything not needed at runtime from `cpflib` to `cpfgen`.
* In `cpflib` implement a method to calculate asymptotic time complexity (like GoogleBenchmark does)
    * Signature shall be something like `std::string complexity_of(auto&& func, std::vector<std::tuple<...>>&& args, std::vector<double>&& arg_sizes)`
    * `arg_sizes` is used to determine the size of the input for each argument, and thus determine the time complexity
* Optimizing the generated parser and visit-functions for better performance
    * The earley parser must be optimized to be as fast as possible
    * The visit functions need to have constant lookup time for dispatch
* License attribution for doctest, google benchmark, and CPM
* `wiki` subfolder with complete structured Markdown documentation
* Ask AI which features to implement next, and which features to prioritize
