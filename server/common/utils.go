package common

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
