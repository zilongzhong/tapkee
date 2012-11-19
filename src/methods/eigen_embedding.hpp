/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Copyright (c) 2012, Sergey Lisitsyn
 *
 * Randomized eigendecomposition code is inspired by the redsvd library
 * code which is distributed under BSD 3-clause license.
 *
 * Copyright (c) 2010, Daisuke Okanohara
 *
 */

#ifndef TAPKEE_EIGEN_EMBEDDING_H_
#define TAPKEE_EIGEN_EMBEDDING_H_

#include "../defines.hpp"
#include "../utils/time.hpp"
#ifndef TAPKEE_NO_ARPACK
	#include "../utils/arpack_wrapper.hpp"
#endif
#include "matrix_operations.hpp"

namespace eigen_embedding_internal
{

//! Templated implementation of eigendecomposition-based embedding. 
template <class MatrixType, class MatrixTypeOperation, int IMPLEMENTATION> 
struct eigen_embedding_impl
{
	//! Construct embedding
	//! @param wm weight matrix to eigendecompose
	//! @param target_dimension target dimension of embedding (number of eigenvectors to find)
	//! @param skip number of eigenvectors to skip
	//!
	EmbeddingResult embed(const MatrixType& wm, unsigned int target_dimension, unsigned int skip);
};

//! ARPACK implementation of eigendecomposition-based embedding
template <class MatrixType, class MatrixTypeOperation> 
struct eigen_embedding_impl<MatrixType, MatrixTypeOperation, ARPACK>
{
	EmbeddingResult embed(const MatrixType& wm, unsigned int target_dimension, unsigned int skip)
	{
		timed_context context("ARPACK DSXUPD eigendecomposition");

#ifdef TAPKEE_NO_ARPACK
		return EmbeddingResult();
#else
		ArpackGeneralizedSelfAdjointEigenSolver<MatrixType, MatrixType, MatrixTypeOperation> arpack(wm,target_dimension+skip,MatrixTypeOperation::ARPACK_CODE);

		DenseMatrix embedding_feature_matrix = (arpack.eigenvectors()).block(0,skip,wm.cols(),target_dimension);

		return EmbeddingResult(embedding_feature_matrix,arpack.eigenvalues().tail(target_dimension));
#endif
	}
};

//! Eigen library dense implementation of eigendecomposition-based embedding
template <class MatrixType, class MatrixTypeOperation> 
struct eigen_embedding_impl<MatrixType, MatrixTypeOperation, EIGEN_DENSE_SELFADJOINT_SOLVER>
{
	EmbeddingResult embed(const MatrixType& wm, unsigned int target_dimension, unsigned int skip)
	{
		timed_context context("Eigen library dense eigendecomposition");

		DenseMatrix dense_wm = wm;
		DefaultDenseSelfAdjointEigenSolver solver(dense_wm);

		DenseMatrix embedding_feature_matrix = (solver.eigenvectors()).block(0,skip,wm.cols(),target_dimension);

		return EmbeddingResult(embedding_feature_matrix,solver.eigenvalues().tail(target_dimension));
	}
};

//! Randomized redsvd-like implementation of eigendecomposition-based embedding
template <class MatrixType, class MatrixTypeOperation> 
struct eigen_embedding_impl<MatrixType, MatrixTypeOperation, RANDOMIZED>
{
	EmbeddingResult embed(const MatrixType& wm, unsigned int target_dimension, unsigned int skip)
	{
		timed_context context("Randomized eigendecomposition");
		
		DenseMatrix O(wm.rows(), target_dimension+skip);
		for (unsigned int i=0; i<O.rows(); ++i)
		{
			unsigned int j=0;
			for ( ; j+1 < O.cols(); j+= 2)
			{
				DefaultScalarType v1 = (DefaultScalarType)(rand()+1.f)/((float)RAND_MAX+2.f);
				DefaultScalarType v2 = (DefaultScalarType)(rand()+1.f)/((float)RAND_MAX+2.f);
				DefaultScalarType len = sqrt(-2.f*log(v1));
				O(i,j) = len*cos(2.f*M_PI*v2);
				O(i,j+1) = len*sin(2.f*M_PI*v2);
			}
			for ( ; j < O.cols(); j++)
			{
				DefaultScalarType v1 = (DefaultScalarType)(rand()+1.f)/((float)RAND_MAX+2.f);
				DefaultScalarType v2 = (DefaultScalarType)(rand()+1.f)/((float)RAND_MAX+2.f);
				DefaultScalarType len = sqrt(-2.f*log(v1));
				O(i,j) = len*cos(2.f*M_PI*v2);
			}
		}
		MatrixTypeOperation operation(wm);

		DenseMatrix Y = operation(O);
		for (unsigned int i=0; i<Y.cols(); i++)
		{
			for (unsigned int j=0; j<i; j++)
			{
				DefaultScalarType r = Y.col(i).dot(Y.col(j));
				Y.col(i) -= r*Y.col(j);
			}
			DefaultScalarType norm = Y.col(i).norm();
			if (norm < 1e-4)
			{
				for (int k = i; k<Y.cols(); k++)
					Y.col(k).setZero();
			}
			Y.col(i) *= (1.f / norm);
		}

		DenseMatrix B1 = operation(Y);
		DenseMatrix B = Y.householderQr().solve(B1);
		DefaultDenseSelfAdjointEigenSolver eigenOfB(B);
		DenseMatrix embedding = (Y*eigenOfB.eigenvectors()).block(0, skip, wm.cols(), target_dimension);

		return EmbeddingResult(embedding,eigenOfB.eigenvalues());
	}
};

};

//! Multiple implementation handler method for various eigendecomposition methods. 
//!
//! Has three template parameters:
//! MatrixType - class of weight matrix to perform eigendecomposition of
//! MatrixTypeOperation - class of product operation over matrix.
//!
//! In order to compute largest eigenvalues MatrixTypeOperation should provide
//! implementation of operator()(DenseMatrix) which computes right product
//! of the parameter with the MatrixType.
//! 
//! In order to compute smallest eigenvalues MatrixTypeOperation should provide
//! implementation of operator()(DenseMatrix) which solves linear system with
//! given right-hand side part. 
//! 
//! Currently supports three methods:
//!
//! <ul>
//! <li> ARPACK
//! <li> RANDOMIZED
//! <li> EIGEN_DENSE_SELFADJOINT_SOLVER
//! </ul>
//!
//! @param method one of supported eigendecomposition methods
//! @param m matrix to be eigendecomposed 
//! @param target_dimension target dimension of embedding i.e. number of eigenvectors to be
//!        computed
//! @param skip number of eigenvectors to skip (from either smallest or largest side)
//!
template <class MatrixType, class MatrixTypeOperation>
EmbeddingResult eigen_embedding(TAPKEE_EIGEN_EMBEDDING_METHOD method, const MatrixType& m, 
                                unsigned int target_dimension, unsigned int skip)
{
	switch (method)
	{
		case ARPACK: 
			return eigen_embedding_internal::
				eigen_embedding_impl<MatrixType, MatrixTypeOperation, 
				ARPACK>().embed(m, target_dimension, skip);
		case RANDOMIZED: 
			return eigen_embedding_internal::
				eigen_embedding_impl<MatrixType, MatrixTypeOperation,
				RANDOMIZED>().embed(m, target_dimension, skip);
		case EIGEN_DENSE_SELFADJOINT_SOLVER:
			return eigen_embedding_internal::
				eigen_embedding_impl<MatrixType, MatrixTypeOperation, 
				EIGEN_DENSE_SELFADJOINT_SOLVER>().embed(m, target_dimension, skip);
	}
	return EmbeddingResult();
};
#endif
