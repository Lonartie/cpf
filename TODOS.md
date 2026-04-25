# TODO's

* Adjust cpf_link_grammars to accept an optional namespace argument
    * All generated classes should be placed within the specified namespace
    * This will help to avoid naming conflicts and improve organization of generated classes
* README update
    * Specify space and runtime complexity of the earley parser
    * Revise README to not go into too much detail about the implementation, keep it clean and focused
* From grammar analysis while generation estimate the space and runtime complexity of 
  the generated classes and include this information in the generated code as documentation
* Benchmark the performance of the generated classes to gather metrics on runtime
* String format: Allow single quotes as well as double quotes for string literals in the grammar
