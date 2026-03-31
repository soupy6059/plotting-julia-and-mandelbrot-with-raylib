# Plotting Juliasets with C++ and Raylib

Using parallelism (upto ```std::thread::hardware_concurrency()``` many "working" threads) and concurrency (no spinning, inactive threads wait on a semaphore for the right to begin computing the julia set).
If a thread encounters too much work, it'll spawn a new thread an delegate it half of it's own.

Uses ```core::implicit_cast<type>(...)``` to minimize use of ```static_cast<type>(...)```, as it's too strong to be casting numbers. Don't eat pancakes with a shotgun, basically.

Uses ```std::memory_order::(...)``` to manage the ```std::atomic<uint64_t>``` lazily. This is also where we prevent spinning by waiting on if the atomic is mutated.

<img width="985" height="986" alt="image" src="https://github.com/user-attachments/assets/71bf158b-b051-4651-9faf-5addcddf0b88" />


```
main: ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux), dynamically linked, interpreter /nix/store/maxa3xhmxggrc5v2vc0c3pjb79hjlkp9-glibc-2.40-66/lib/ld-linux-x86-64.so.2, for GNU/Linux 3.10.0, with debug_info, not stripped
```
