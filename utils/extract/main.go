package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"unsafe"
)

type record struct {
	rev      uint32
	ts       uint32
	logHours float64
	hzHrs    float64
	voltHrs  [15]float64
	wattHrs  [15]float64
	vaHrs    [15]float64
}

func main() {
	outFlag := flag.String("o", "", "output file (defaults to input file with .csv extension)")
	flag.Parse()

	if flag.NArg() < 1 {
		_, _ = fmt.Fprintln(os.Stderr, "usage: extract [-o outputfile] <inputfile>")
		os.Exit(1)
	}
	inputPath := flag.Arg(0)

	outputPath := *outFlag
	if outputPath == "" {
		ext := filepath.Ext(inputPath)
		outputPath = strings.TrimSuffix(inputPath, ext) + ".csv"
	}

	ifile, err := os.Open(filepath.Clean(inputPath))
	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "could not open input file: %v\n", err)
		os.Exit(1)
	}
	defer func() { _ = ifile.Close() }()

	ofile, err := os.OpenFile(filepath.Clean(outputPath), os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o644)
	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "could not open output file: %v\n", err)
		os.Exit(1)
	}
	defer func() {
		if err = ofile.Close(); err != nil {
			_, _ = fmt.Fprintf(os.Stderr, "could not close output file: %v\n", err)
		}
	}()

	recSize := unsafe.Sizeof(record{})
	buf := make([]byte, recSize)

	// Write CSV header
	header := "Revision,Timestamp,LogHours,HzHours"
	for i := 0; i < 15; i++ {
		header += fmt.Sprintf(",VoltHrs%d,WattHrs%d,VaHrs%d", i+1, i+1, i+1)
	}
	header += "\n"
	if _, err = ofile.WriteString(header); err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "could not write to output file: %v\n", err)
		os.Exit(1)
	}

	for {
		n, err := ifile.Read(buf)
		if err != nil {
			if err.Error() != "EOF" {
				_, _ = fmt.Fprintf(os.Stderr, "error reading input file: %v\n", err)
			}
			break
		}
		if n != int(recSize) {
			_, _ = fmt.Fprintf(os.Stderr, "incomplete record read: expected %d bytes, got %d bytes\n", recSize, n)
			break
		}

		var rec record
		rec = *(*record)(unsafe.Pointer(&buf[0]))

		line := fmt.Sprintf("%d,%d,%.6f,%.4f", rec.rev, rec.ts, rec.logHours, rec.hzHrs)
		for i := 0; i < 15; i++ {
			line += fmt.Sprintf(",%.3f,%.3f,%.3f", rec.voltHrs[i], rec.wattHrs[i], rec.vaHrs[i])
		}
		line += "\n"

		if _, err = ofile.WriteString(line); err != nil {
			_, _ = fmt.Fprintf(os.Stderr, "could not write to output file: %v\n", err)
			os.Exit(1)
		}
	}
}
