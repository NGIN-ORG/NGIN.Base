#pragma once

/// @file Matrix.hpp
/// @brief Fixed-size row-major matrices and fundamental linear algebra operations.

#include <NGIN/Math/Vector.hpp>

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <optional>
#include <utility>

namespace NGIN::Math
{
    /// @brief A contiguous, row-major, fixed-size mathematical matrix.
    /// @tparam T Element type.
    /// @tparam RowCount Number of rows; must be greater than zero.
    /// @tparam ColumnCount Number of columns; must be greater than zero.
    template<LinearAlgebraScalarConcept T, std::size_t RowCount, std::size_t ColumnCount>
    class Matrix
    {
        static_assert(RowCount > 0, "A matrix must have at least one row");
        static_assert(ColumnCount > 0, "A matrix must have at least one column");

    public:
        /// @brief Element type.
        using ValueType = T;

        /// @brief Contiguous row-major storage.
        using StorageType = std::array<T, RowCount * ColumnCount>;

        /// @brief Number of rows.
        static constexpr std::size_t ROWS = RowCount;

        /// @brief Number of columns.
        static constexpr std::size_t COLUMNS = ColumnCount;

        /// @brief Constructs the zero matrix.
        constexpr Matrix() = default;

        /// @brief Constructs a row-major matrix from exactly one value per element.
        template<class... Values>
            requires(sizeof...(Values) == RowCount * ColumnCount &&
                     (std::constructible_from<T, Values &&> && ...))
        constexpr explicit Matrix(Values&&... values)
            : m_values {static_cast<T>(std::forward<Values>(values))...}
        {
        }

        /// @brief Constructs a row-major matrix from contiguous array storage.
        constexpr explicit Matrix(StorageType values) : m_values(std::move(values)) {}

        /// @brief Explicitly converts every element of another matrix.
        template<LinearAlgebraScalarConcept U>
            requires std::constructible_from<T, const U&>
        constexpr explicit Matrix(const Matrix<U, RowCount, ColumnCount>& other)
        {
            for (std::size_t row = 0; row < RowCount; ++row)
                for (std::size_t column = 0; column < ColumnCount; ++column)
                    (*this)(row, column) = static_cast<T>(other(row, column));
        }

        /// @brief Creates a matrix with every element set to the same value.
        [[nodiscard]] static constexpr Matrix Filled(const T& value)
        {
            Matrix result;
            result.m_values.fill(value);
            return result;
        }

        /// @brief Creates an identity matrix.
        [[nodiscard]] static constexpr Matrix Identity()
            requires(RowCount == ColumnCount)
        {
            Matrix result;
            for (std::size_t index = 0; index < RowCount; ++index)
                result(index, index) = T {1};
            return result;
        }

        /// @brief Returns mutable contiguous row-major storage.
        [[nodiscard]] constexpr T* Data() noexcept { return m_values.data(); }

        /// @brief Returns immutable contiguous row-major storage.
        [[nodiscard]] constexpr const T* Data() const noexcept { return m_values.data(); }

        /// @brief Returns the fixed number of rows.
        [[nodiscard]] static constexpr std::size_t Rows() noexcept { return RowCount; }

        /// @brief Returns the fixed number of columns.
        [[nodiscard]] static constexpr std::size_t Columns() noexcept { return ColumnCount; }

        /// @brief Returns the fixed number of elements.
        [[nodiscard]] static constexpr std::size_t Size() noexcept { return RowCount * ColumnCount; }

        /// @brief Returns a mutable element by zero-based row and column.
        [[nodiscard]] constexpr T& operator()(std::size_t row, std::size_t column) noexcept
        {
            assert(row < RowCount);
            assert(column < ColumnCount);
            return m_values[row * ColumnCount + column];
        }

        /// @brief Returns an immutable element by zero-based row and column.
        [[nodiscard]] constexpr const T& operator()(std::size_t row, std::size_t column) const noexcept
        {
            assert(row < RowCount);
            assert(column < ColumnCount);
            return m_values[row * ColumnCount + column];
        }

        /// @brief Returns a copy of one row.
        [[nodiscard]] constexpr Vector<T, ColumnCount> Row(std::size_t row) const
        {
            assert(row < RowCount);
            Vector<T, ColumnCount> result;
            for (std::size_t column = 0; column < ColumnCount; ++column)
                result[column] = (*this)(row, column);
            return result;
        }

        /// @brief Returns a copy of one column.
        [[nodiscard]] constexpr Vector<T, RowCount> Column(std::size_t column) const
        {
            assert(column < ColumnCount);
            Vector<T, RowCount> result;
            for (std::size_t row = 0; row < RowCount; ++row)
                result[row] = (*this)(row, column);
            return result;
        }

        /// @brief Replaces one row.
        constexpr void SetRow(std::size_t row, const Vector<T, ColumnCount>& value)
        {
            assert(row < RowCount);
            for (std::size_t column = 0; column < ColumnCount; ++column)
                (*this)(row, column) = value[column];
        }

        /// @brief Replaces one column.
        constexpr void SetColumn(std::size_t column, const Vector<T, RowCount>& value)
        {
            assert(column < ColumnCount);
            for (std::size_t row = 0; row < RowCount; ++row)
                (*this)(row, column) = value[row];
        }

        /// @brief Returns an iterator to the first element.
        [[nodiscard]] constexpr auto begin() noexcept { return m_values.begin(); }

        /// @brief Returns an immutable iterator to the first element.
        [[nodiscard]] constexpr auto begin() const noexcept { return m_values.begin(); }

        /// @brief Returns an iterator past the last element.
        [[nodiscard]] constexpr auto end() noexcept { return m_values.end(); }

        /// @brief Returns an immutable iterator past the last element.
        [[nodiscard]] constexpr auto end() const noexcept { return m_values.end(); }

        /// @brief Adds another matrix element-wise.
        constexpr Matrix& operator+=(const Matrix& other)
        {
            for (std::size_t index = 0; index < Size(); ++index)
                m_values[index] += other.m_values[index];
            return *this;
        }

        /// @brief Subtracts another matrix element-wise.
        constexpr Matrix& operator-=(const Matrix& other)
        {
            for (std::size_t index = 0; index < Size(); ++index)
                m_values[index] -= other.m_values[index];
            return *this;
        }

        /// @brief Multiplies every element by a scalar.
        constexpr Matrix& operator*=(const T& scalar)
        {
            for (T& element: m_values)
                element *= scalar;
            return *this;
        }

        /// @brief Divides every element by a scalar.
        constexpr Matrix& operator/=(const T& scalar)
        {
            for (T& element: m_values)
                element /= scalar;
            return *this;
        }

        /// @brief Compares matrices element-wise for exact equality.
        [[nodiscard]] constexpr bool operator==(const Matrix&) const = default;

    private:
        StorageType m_values {};
    };

    /// @brief Adds two matrices element-wise.
    template<LinearAlgebraScalarConcept T, std::size_t Rows, std::size_t Columns>
    [[nodiscard]] constexpr Matrix<T, Rows, Columns> operator+(
            Matrix<T, Rows, Columns>        left,
            const Matrix<T, Rows, Columns>& right)
    {
        left += right;
        return left;
    }

    /// @brief Subtracts two matrices element-wise.
    template<LinearAlgebraScalarConcept T, std::size_t Rows, std::size_t Columns>
    [[nodiscard]] constexpr Matrix<T, Rows, Columns> operator-(
            Matrix<T, Rows, Columns>        left,
            const Matrix<T, Rows, Columns>& right)
    {
        left -= right;
        return left;
    }

    /// @brief Negates every matrix element.
    template<LinearAlgebraScalarConcept T, std::size_t Rows, std::size_t Columns>
    [[nodiscard]] constexpr Matrix<T, Rows, Columns> operator-(const Matrix<T, Rows, Columns>& value)
    {
        Matrix<T, Rows, Columns> result;
        for (std::size_t row = 0; row < Rows; ++row)
            for (std::size_t column = 0; column < Columns; ++column)
                result(row, column) = -value(row, column);
        return result;
    }

    /// @brief Multiplies every matrix element by a scalar.
    template<LinearAlgebraScalarConcept T, std::size_t Rows, std::size_t Columns>
    [[nodiscard]] constexpr Matrix<T, Rows, Columns> operator*(Matrix<T, Rows, Columns> value, const T& scalar)
    {
        value *= scalar;
        return value;
    }

    /// @brief Multiplies every matrix element by a scalar.
    template<LinearAlgebraScalarConcept T, std::size_t Rows, std::size_t Columns>
    [[nodiscard]] constexpr Matrix<T, Rows, Columns> operator*(const T& scalar, Matrix<T, Rows, Columns> value)
    {
        value *= scalar;
        return value;
    }

    /// @brief Divides every matrix element by a scalar.
    template<LinearAlgebraScalarConcept T, std::size_t Rows, std::size_t Columns>
    [[nodiscard]] constexpr Matrix<T, Rows, Columns> operator/(Matrix<T, Rows, Columns> value, const T& scalar)
    {
        value /= scalar;
        return value;
    }

    /// @brief Multiplies compatible matrices.
    template<LinearAlgebraScalarConcept T, std::size_t Rows, std::size_t Inner, std::size_t Columns>
    [[nodiscard]] constexpr Matrix<T, Rows, Columns> operator*(
            const Matrix<T, Rows, Inner>&    left,
            const Matrix<T, Inner, Columns>& right)
    {
        Matrix<T, Rows, Columns> result;
        for (std::size_t row = 0; row < Rows; ++row)
        {
            for (std::size_t column = 0; column < Columns; ++column)
            {
                T value {0};
                for (std::size_t index = 0; index < Inner; ++index)
                    value += left(row, index) * right(index, column);
                result(row, column) = std::move(value);
            }
        }
        return result;
    }

    /// @brief Transforms a column vector by a compatible matrix.
    template<LinearAlgebraScalarConcept T, std::size_t Rows, std::size_t Columns>
    [[nodiscard]] constexpr Vector<T, Rows> operator*(
            const Matrix<T, Rows, Columns>& matrix,
            const Vector<T, Columns>&       vector)
    {
        Vector<T, Rows> result;
        for (std::size_t row = 0; row < Rows; ++row)
            result[row] = Dot(matrix.Row(row), vector);
        return result;
    }

    /// @brief Returns a matrix with rows and columns exchanged.
    template<LinearAlgebraScalarConcept T, std::size_t Rows, std::size_t Columns>
    [[nodiscard]] constexpr Matrix<T, Columns, Rows> Transpose(const Matrix<T, Rows, Columns>& value)
    {
        Matrix<T, Columns, Rows> result;
        for (std::size_t row = 0; row < Rows; ++row)
            for (std::size_t column = 0; column < Columns; ++column)
                result(column, row) = value(row, column);
        return result;
    }

    /// @brief Returns the sum of the diagonal elements of a square matrix.
    template<LinearAlgebraScalarConcept T, std::size_t Size>
    [[nodiscard]] constexpr T Trace(const Matrix<T, Size, Size>& value)
    {
        T result {0};
        for (std::size_t index = 0; index < Size; ++index)
            result += value(index, index);
        return result;
    }

    /// @brief Computes the determinant using fraction-free Gaussian elimination.
    /// @details Division must be exact for integral-like element types.
    template<LinearAlgebraScalarConcept T, std::size_t Size>
    [[nodiscard]] constexpr T Determinant(const Matrix<T, Size, Size>& value)
    {
        if constexpr (Size == 1)
        {
            return value(0, 0);
        }
        else
        {
            Matrix<T, Size, Size> work = value;
            T                     previousPivot {1};
            T                     sign {1};

            for (std::size_t pivotIndex = 0; pivotIndex + 1 < Size; ++pivotIndex)
            {
                std::size_t pivotRow = pivotIndex;
                while (pivotRow < Size && work(pivotRow, pivotIndex) == T {0})
                    ++pivotRow;
                if (pivotRow == Size)
                    return T {0};

                if (pivotRow != pivotIndex)
                {
                    const Vector<T, Size> row = work.Row(pivotIndex);
                    work.SetRow(pivotIndex, work.Row(pivotRow));
                    work.SetRow(pivotRow, row);
                    sign = -sign;
                }

                const T pivot = work(pivotIndex, pivotIndex);
                for (std::size_t row = pivotIndex + 1; row < Size; ++row)
                {
                    for (std::size_t column = pivotIndex + 1; column < Size; ++column)
                    {
                        work(row, column) =
                                (work(row, column) * pivot - work(row, pivotIndex) * work(pivotIndex, column)) /
                                previousPivot;
                    }
                    work(row, pivotIndex) = T {0};
                }
                previousPivot = pivot;
            }

            return sign * work(Size - 1, Size - 1);
        }
    }

    /// @brief Returns an inverse, or no value when no pivot exceeds the supplied absolute tolerance.
    /// @details The element type must model a field; integer division generally does not satisfy that contract.
    template<LinearAlgebraScalarConcept T, std::size_t Size>
        requires std::totally_ordered<T>
    [[nodiscard]] constexpr std::optional<Matrix<T, Size, Size>> TryInverse(
            const Matrix<T, Size, Size>& value,
            T                            tolerance = T {0})
    {
        const auto absolute = [](const T& candidate) { return candidate < T {0} ? -candidate : candidate; };

        Matrix<T, Size, Size> work              = value;
        Matrix<T, Size, Size> inverse           = Matrix<T, Size, Size>::Identity();
        const T               absoluteTolerance = absolute(tolerance);

        for (std::size_t pivotIndex = 0; pivotIndex < Size; ++pivotIndex)
        {
            std::size_t pivotRow       = pivotIndex;
            T           pivotMagnitude = absolute(work(pivotRow, pivotIndex));
            for (std::size_t row = pivotIndex + 1; row < Size; ++row)
            {
                const T candidateMagnitude = absolute(work(row, pivotIndex));
                if (candidateMagnitude > pivotMagnitude)
                {
                    pivotMagnitude = candidateMagnitude;
                    pivotRow       = row;
                }
            }

            if (pivotMagnitude <= absoluteTolerance)
                return std::nullopt;

            if (pivotRow != pivotIndex)
            {
                const Vector<T, Size> workRow = work.Row(pivotIndex);
                work.SetRow(pivotIndex, work.Row(pivotRow));
                work.SetRow(pivotRow, workRow);

                const Vector<T, Size> inverseRow = inverse.Row(pivotIndex);
                inverse.SetRow(pivotIndex, inverse.Row(pivotRow));
                inverse.SetRow(pivotRow, inverseRow);
            }

            const T pivot = work(pivotIndex, pivotIndex);
            work.SetRow(pivotIndex, work.Row(pivotIndex) / pivot);
            inverse.SetRow(pivotIndex, inverse.Row(pivotIndex) / pivot);

            for (std::size_t row = 0; row < Size; ++row)
            {
                if (row == pivotIndex)
                    continue;

                const T factor = work(row, pivotIndex);
                if (factor == T {0})
                    continue;
                work.SetRow(row, work.Row(row) - factor * work.Row(pivotIndex));
                inverse.SetRow(row, inverse.Row(row) - factor * inverse.Row(pivotIndex));
            }
        }

        return inverse;
    }

    /// @brief Two-by-two matrix alias.
    template<LinearAlgebraScalarConcept T>
    using Matrix2 = Matrix<T, 2, 2>;

    /// @brief Three-by-three matrix alias.
    template<LinearAlgebraScalarConcept T>
    using Matrix3 = Matrix<T, 3, 3>;

    /// @brief Four-by-four matrix alias.
    template<LinearAlgebraScalarConcept T>
    using Matrix4 = Matrix<T, 4, 4>;

    /// @brief Common single-precision matrix aliases.
    using Matrix2F = Matrix2<F32>;
    using Matrix3F = Matrix3<F32>;
    using Matrix4F = Matrix4<F32>;

    /// @brief Common double-precision matrix aliases.
    using Matrix2D = Matrix2<F64>;
    using Matrix3D = Matrix3<F64>;
    using Matrix4D = Matrix4<F64>;
}// namespace NGIN::Math
