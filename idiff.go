package main

import (
	"os"
	"fmt"
)

func main() {
	if len(os.Args) < 3 {
		fmt.Printf("Usage: %s <left> <right> [diff.html]\n", os.Args[0])
		os.Exit(1)
	}

	left := os.Args[1]
	right := os.Args[2]
	diff := "diff.html"
	if len(os.Args) > 3 {
		diff = os.Args[3]
	}
}
