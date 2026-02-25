package common

import "github.com/pion/rtp"

func Contains[T comparable](s []T, e T) bool {
	for _, a := range s {
		if a == e {
			return true
		}
	}

	return false
}

func Collect[T any, R any](s []T, f func(e T) R) []R {
	r := make([]R, 0)
	for _, e := range s {
		r = append(r, f(e))
	}

	return r
}

type RtpExtentions struct {
	Extention  bool
	EXtentions []rtp.Extension
}

func Reverse[S []E, E any](s S) S {
	ret := make(S, 0)
	for i := len(s) - 1; i >= 0; i-- {
		ret = append(ret, s[i])
	}

	return ret
}
