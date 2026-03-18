# C++ Chess Engine ♟️

A C++ chess engine with a graphical interface built using **SFML**. Currently supports a two-player mode with intuitive drag-and-drop piece movement, full legal move validation, and a visually appealing chessboard. Future updates will focus on implementing a challenging AI opponent.

---

## ✨ Features

* ✅ **Graphical UI**: A clean and responsive interface built with **SFML**.
* ✅ **Drag-and-Drop Controls**: Easily move pieces by dragging them to a valid square.
* ✅ **Turn-Based Logic**: Enforces alternating turns between White and Black.
* ✅ **Legal Move Validation**: Comprehensive move validation for all pieces, including special moves like castling, en passant, and pawn promotion.

---

## 🛠️ Installation & Setup

Follow these steps to compile and run the project on your local machine.

### 1. Clone the Repository

```bash
git clone https://github.com/ashish-jha-73/Chess-Engine.git
cd Chess-Engine
```

### 2. Install SFML

Install the SFML development libraries for your operating system.

* **On Ubuntu/Debian:**
    ```bash
    sudo apt install libsfml-dev
    ```
* **On Arch Linux:**
    ```bash
    sudo pacman -S sfml
    ```
* **On macOS (using Homebrew):**
    ```bash
    brew install sfml
    ```

### 3. Build the Project

Compile the source code using g++.

```bash
make
```

### 4. Run the Game

Execute the compiled binary.

```bash
make run
```
or 

```bash
./chessgame --graphics
```

---

## 🧠 Planned Features

Here's a roadmap of upcoming enhancements:

* 🔄 **AI Opponent**: A computer opponent using the **Minimax** algorithm with **Alpha-Beta Pruning**.
* 🔄 **Move History**: A system to track moves.
* 🔄 **Undo**: Functionality to take back moves.

---

## 📄 License

This project is licensed under the **MIT License**. You are free to use, modify, and share this project.

---

