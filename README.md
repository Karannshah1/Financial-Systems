# Financial Systems

This project implements a Financial Transaction Processing system using advanced techniques like Transactional Memory-aware Scheduler, Speculative Execution, and Software Transactional Memory (STM). The main code is contained in `Financial_transactions.cpp`.

## Table of Contents

- [Features](#features)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Usage](#usage)
- [Example](#example)
- [Contributing](#contributing)

## Features

- **Transactional Memory-aware Scheduler:** Efficient scheduling of transactions to optimize performance and reduce conflicts.
- **Speculative Execution:** Enhances the throughput by predicting and executing transactions ahead of time.
- **Software Transactional Memory (STM):** Ensures safe concurrent access to shared memory without traditional locks, improving scalability and simplicity.

## Prerequisites

To build and run this project, you will need:

- A C++ compiler (GCC, Clang, or MSVC)
- CMake (for building the project)
- Git (to clone the repository)

## Installation

1. **Clone the repository:**
    ```sh
    git clone https://github.com/Karannshah1/Financial-Systems.git
    cd Financial-Systems
    ```

2. **Build the project:**
    ```sh
    mkdir build
    cd build
    cmake ..
    make
    ```

## Usage

1. **Run the executable:**
    ```sh
    ./Financial_transactions
    ```

2. **Configuration:**
    - The configuration for transactions, scheduling, and STM parameters can be adjusted in the `Financial_transactions.cpp` file.
    - Ensure to rebuild the project after making any changes to the source code:
        ```sh
        make
        ```

## Example

Here is a simple example of how to execute a financial transaction using this system:

```cpp
#include "Financial_transactions.h"

int main() {
    // Initialize the system
    TransactionSystem ts;

    // Create and execute a transaction
    ts.beginTransaction();
    ts.performTransaction(/* transaction details */);
    ts.commitTransaction();

    return 0;
}
```

## Contributing

Contributions are welcome! Please follow these steps to contribute:

1. Fork the repository
2. Create a new branch (git checkout -b feature-branch)
3. Make your changes
4. Commit your changes (git commit -m 'Add some feature')
5. Push to the branch (git push origin feature-branch)
6. Create a new Pull Request
