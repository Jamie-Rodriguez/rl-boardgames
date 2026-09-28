#ifndef UTILS_H
#define UTILS_H

/**
 * Compile-time assertion for C99 (no _Static_assert): a false `expr` declares
 * an array of negative size. `name` must be a unique identifier per scope.
 */
#define STATIC_ASSERT(expr, name) \
	typedef char static_assert_##name[(expr) ? 1 : -1]

#endif /* UTILS_H */
