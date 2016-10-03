package main

import (
	"bytes"
	"fmt"
	"image"
	_ "image/png"
	"io/ioutil"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
)

type Diff struct {
	l, r string
	diff float64
}
type DiffSlice []Diff

func (ds DiffSlice) Len() int           { return len(ds) }
func (ds DiffSlice) Less(i, j int) bool { return ds[i].diff > ds[j].diff }
func (ds DiffSlice) Swap(i, j int)      { ds[i], ds[j] = ds[j], ds[i] }

func IsEasy(i image.Image) *image.NRGBA {
	switch v := i.(type) {
	case *image.NRGBA:
		if v.Stride == 4*(v.Rect.Max.X-v.Rect.Min.X) {
			return v
		}
		return nil
	default:
		return nil
	}
}

func Abs(x int64) int64 {
	mask := x >> 63
	return (x ^ mask) - mask
}

func AbsDiff(x, y int64) int64 {
	return Abs(x - y)
}

func DiffImagesEasy(l, r *image.NRGBA) float64 {
	sad := int64(0)
	for i := range l.Pix {
		sad += AbsDiff(int64(l.Pix[i]), int64(r.Pix[i]))
	}
	return float64(sad) / float64(len(l.Pix)*0xff)
}

func DiffImages(l, r image.Image) float64 {
	if !l.Bounds().Eq(r.Bounds()) {
		return math.Inf(+1)
	}

	if ezL, ezR := IsEasy(l), IsEasy(r); ezL != nil && ezR != nil {
		return DiffImagesEasy(ezL, ezR)
	}

	x0, x1 := l.Bounds().Min.X, l.Bounds().Max.X
	y0, y1 := l.Bounds().Min.Y, l.Bounds().Max.Y

	sad := int64(0)
	for y := y0; y < y1; y++ {
		for x := x0; x < x1; x++ {
			lr, lg, lb, la := l.At(x, y).RGBA()
			rr, rg, rb, ra := r.At(x, y).RGBA()
			sad += AbsDiff(int64(lr), int64(rr))
			sad += AbsDiff(int64(lg), int64(rg))
			sad += AbsDiff(int64(lb), int64(rb))
			sad += AbsDiff(int64(la), int64(ra))
		}
	}
	return float64(sad) / float64(4*(x1-x0)*(y1-y0)*0xffff)
}

func main() {
	if len(os.Args) < 3 {
		fmt.Printf("Usage: %s <left> <right> [diff.html]\n", os.Args[0])
		os.Exit(1)
	}

	left := filepath.Clean(os.Args[1])
	right := filepath.Clean(os.Args[2])
	diff := "diff.html"
	diffbase := filepath.Base(diff)
	if len(os.Args) > 3 {
		diff = os.Args[3]
	}

	wg := &sync.WaitGroup{}
	diffs := make(DiffSlice, 0)
	mutex := &sync.Mutex{}
	compareCnt := int32(0)
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

			atomic.AddInt32(&compareCnt, 1)

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

			if diff := DiffImages(li, ri); diff > 0 {
				left, _ := filepath.Rel(diffbase, path)
				right, _ := filepath.Rel(diffbase, rpath)
				mutex.Lock()
				diffs = append(diffs, Diff{left, right, diff})
				mutex.Unlock()
			}
		}()
		return nil
	})
	wg.Wait()

	fmt.Println(compareCnt - int32(len(diffs)), "files are identical.", )
	if len(diffs) == 0 {
		os.Exit(1)
	}

	sort.Sort(diffs)

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
	for i := 0; i < len(diffs); i++ {
		fmt.Fprintf(df,
			`<tr><td><div><img src=%s><img src=%s style="position:absolute; mix-blend-mode:difference"></div>
			     <td><a href=%s><img src=%s></a>
			     <td><a href=%s><img src=%s></a>`,
			diffs[i].l, diffs[i].r,
			diffs[i].l, diffs[i].l,
			diffs[i].r, diffs[i].r)
	}

	fmt.Println(len(diffs), "diffs written to", diff)
}
