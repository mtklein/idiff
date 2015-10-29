package main

import (
	"bytes"
	"fmt"
	"image"
	_ "image/png"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"sync"
)

func main() {
	if len(os.Args) < 3 {
		fmt.Printf("Usage: %s <left> <right> [diff.html]\n", os.Args[0])
		os.Exit(1)
	}

	left := filepath.Clean(os.Args[1])
	right := filepath.Clean(os.Args[2])
	diff := "diff.html"
	if len(os.Args) > 3 {
		diff = os.Args[3]
	}

	wg := &sync.WaitGroup{}
	diffs := make([]string, 0)
	mutex := &sync.Mutex{}
	filepath.Walk(left, func(path string, info os.FileInfo, err error) error {
		path = filepath.Clean(path)
		if err != nil {
			return err
		}
		wg.Add(1)
		go func() {
			defer wg.Done()

			lb, err := ioutil.ReadFile(path)
			if err != nil {
				return
			}

			rpath := strings.Replace(path, left, right, -1)
			rb, err := ioutil.ReadFile(rpath)
			if err != nil {
				fmt.Println("No corresponding file found for", path)
				return
			}

			if bytes.Equal(lb, rb) {
				return
			}

			li, _, err := image.Decode(bytes.NewReader(lb))
			if err != nil {
				return
			}
			ri, _, err := image.Decode(bytes.NewReader(rb))
			if err != nil {
				fmt.Println("Couldn't decode", rpath)
				return
			}

			if !li.Bounds().Eq(ri.Bounds()) {
				mutex.Lock()
				diffs = append(diffs, path, rpath)
				mutex.Unlock()
				return
			}

			x0 := li.Bounds().Min.X
			x1 := li.Bounds().Max.X
			y0 := li.Bounds().Min.Y
			y1 := li.Bounds().Max.Y

			for y := y0; y < y1; y++ {
				for x := x0; x < x1; x++ {
					if li.At(x, y) != ri.At(x, y) {
						mutex.Lock()
						diffs = append(diffs, path, rpath)
						mutex.Unlock()
						return
					}
				}
			}
		}()
		return nil
	})
	wg.Wait()

	if len(diffs) == 0 {
		os.Exit(1)
	}

	df, err := os.Create(diff)
	if err != nil {
		fmt.Println(err)
		return
	}
	defer df.Close()
	style := `
        body { background-size: 16px 16px;
               background-color: rgb(230,230,230);
               background-image:
           linear-gradient(45deg, rgba(255,255,255,.2) 25%, transparent 25%, transparent 50%,
           rgba(255,255,255,.2) 50%, rgba(255,255,255,.2) 75%, transparent 75%, transparent)
        }
	div { position: relative; left: 0; top: 0 }
        table { table-layout:fixed; width:100% }
        img {max-width:100%; max-height:320; left: 0; top: 0 }`

	fmt.Fprintf(df, "<style>%s</style><table>", style)
	for i := 0; i < len(diffs)/2; i++ {
		fmt.Fprintf(df,
			`<tr><td><div><img src=%s><img src=%s style="position:absolute; mix-blend-mode:difference"></div>
			<td><a href=%s><img src=%s></a>
                        <td><a href=%s><img src=%s></a>`,
			diffs[i*2+0], diffs[i*2+1],
			diffs[i*2+0], diffs[i*2+0],
			diffs[i*2+1], diffs[i*2+1])
	}

	fmt.Println(len(diffs)/2, "diffs written to", diff)
}
