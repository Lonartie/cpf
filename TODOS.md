# TODO's

* In `cpflib` implement a method to calculate asymptotic time complexity (like GoogleBenchmark does)
    * Signature shall be something like `cpf::complexity complexity_of(auto&& func, std::vector<std::tuple<...>>&& args, std::vector<double>&& arg_sizes)`
    * `arg_sizes` is used to determine the size of the input for each argument, and thus determine the time complexity
    * `cpf::complexity` shall contain a human-readable string representation of the complexity
      as well as a std::function<double(double)> to compute the estimated time for a given input size, based on the observed data
    * Therefore the `complexity_of` method shall analyze not only asymptotic complexits in big O, but also the constants and lower
      order terms, to provide a more accurate estimation of the time complexity for practical input sizes
* In `cpfgen` implement a method to identify per grammar rule the asymptotic time complexity of that rule
    * By first implementing a method that generates a set of valid inputs with different sizes for each rule
    * Then using the method from `cpflib` to determine the time complexity of each rule based on the generated inputs
      and the sizes of those inputs, counted by chars.
* License attribution for doctest, google benchmark, and CPM
* `wiki` subfolder with complete structured Markdown documentation
* Ask AI which features to implement next, and which features to prioritize
