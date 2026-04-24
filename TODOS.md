# TODO's

* Allow for a grammar to be defined multiple times
    * The rules should effectively merge but keep their respective attributes (also default ones)
    * Even when defined multiple times, only a single class per identifier shall be generated
    * When two definitions would conflict in their member type resolution an error should be emitted
    * The ast node should contain a member representing which definition was used to generate the node
* Nodes should contain information about which source range was matched to generate the node
    * For string members we'd need to introduce a struct to hold the string value and the source range
* Allow for a grammar to be defined in multiple files
    * Enable the use of `import` statements to include other grammar files
* Adjust cpf_link_grammars to accept an optional namespace argument
    * All generated classes should be placed within the specified namespace
    * This will help to avoid naming conflicts and improve organization of generated classes
* Implement support for more complex grammar features, such as:
    * Optional elements
    * Repetitions
        * Zero or more
        * One or more
        * Specific number of repetitions
* Benchmark the performance of the generated classes to gather metrics on runtime
* Specify space and runtime complexity in README
* From grammar analysis while generation estimate the space and runtime complexity of 
  the generated classes and include this information in the generated code as documentation