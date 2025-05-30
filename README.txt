--- base.h ---

Amalgamation of useful C functions, datastructures, allocators, and more, in one single header file.

Contains:
-> sac: Simple Arena Allocator
-> nicc: Datastructures (HashMap, ArrayList, LinkedList)
-> str: Pascal-like strings with a bunch of helper functions. Yoinked from the metagen project.
-> log: Logging. Also yoinked from the metagen project.
-> nag: Nicolai's Amazing Graph library for directed graphs.
-> threadpool (tp)


Every C project I work on invariably ends up using two or more of the above projects. To make it easier to maintain and use, I merged them into a single header file. My own standard library of sorts.
