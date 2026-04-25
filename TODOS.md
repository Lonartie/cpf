# TODO's

* Optimizing the generated parser (still keep earley) and visit-functions for better performance
    * The earley parser must be optimized to be as fast as possible
    * The visit functions need to have constant lookup time for dispatch
    * Use the benchmarks to verify the performance improvements
* In `cpflib` implement a method to calculate asymptotic time complexity (like GoogleBenchmark does)
    * Signature shall be something like `std::string complexity_of(auto&& func, std::vector<std::tuple<...>>&& args, std::vector<double>&& arg_sizes)`
    * `arg_sizes` is used to determine the size of the input for each argument, and thus determine the time complexity
* In `cpfgen` implement a method to identify per grammar rule the asymptotic time complexity of that rule
    * By first implementing a method that generates a set of valid inputs with different sizes for each rule
    * Then using the method from `cpflib` to determine the time complexity of each rule based on the generated inputs
      and the sizes of those inputs, counted by chars.
* License attribution for doctest, google benchmark, and CPM
* `wiki` subfolder with complete structured Markdown documentation
* Ask AI which features to implement next, and which features to prioritize
