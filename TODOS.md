# TODO's

* Make label that defines member variables optional
    * If no label is provided the match should not be captured
* Nodes should contain information about which source range was matched to generate the node
    * For string members we'd need to introduce a struct to hold the string value and the source range
* Allow for a grammar to be defined in multiple files
    * Enable the use of `import` statements to include other grammar files
* Adjust cpf_link_grammars to accept an optional namespace argument
    * All generated classes should be placed within the specified namespace
    * This will help to avoid naming conflicts and improve organization of generated classes
* Specify space and runtime complexity in README
* From grammar analysis while generation estimate the space and runtime complexity of 
  the generated classes and include this information in the generated code as documentation
* Benchmark the performance of the generated classes to gather metrics on runtime
