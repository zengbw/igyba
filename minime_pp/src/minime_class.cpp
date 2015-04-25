#include "minime_class.h"

parameter *minime::alloc_Parameter(const uint n)
{
	parameter *m = static_cast<parameter *>(malloc(n * sizeof(parameter)));
	if(NULL == m)
	{
		error_msg("memory allocation failed. good bye.", ERR_ARG);
		exit(EXIT_FAILURE);
	}
	for(uint i = 0; i < n; ++i)
	{
		m[i].name = "";
		m[i].unit = "";
		m[i].init =
		m[i].minv =
		m[i].maxv =
		m[i].val = 0.;
		m[i].log =
		m[i].fit = 0;
	}
	return m;
}

void minime::print_Parameter(const parameter *pp)
{
	if((*pp).unit.length() != 5 && (*pp).unit.compare("pixel"))
		iprint(stdout,
				"> parameter name: %s / %s\n" \
				"init: %g\n" \
				"min: %g\n" \
				"max: %g\n" \
				"val: %g\n" \
				"log: %u\n" \
				"fit: %u\n",
				(*pp).name.c_str(), (*pp).unit.c_str(),
				(*pp).init, (*pp).minv, (*pp).maxv, (*pp).val,
				(*pp).log, (*pp).fit);
	else
		iprint(stdout,
				"> parameter name: %s / %s\n" \
				"init: %g\n" \
				"min: %g\n" \
				"max: %g\n" \
				"val: %g\n" \
				"log: %u\n" \
				"fit: %u\n",
				(*pp).name.c_str(), "um",
				(*pp).init * scl,
				(*pp).minv * scl, (*pp).maxv * scl, (*pp).val * scl,
				(*pp).log, (*pp).fit);
}

minime::minime(void)
{
	lambda = 0xDEADDEAD;
	scl = 1.;

	data = nullptr;

	linregpar.aoff =
	linregpar.bmul =
	linregpar.siga =
	linregpar.sigb =
	linregpar.sigdat =
	linregpar.ssq =
	linregpar.corr = 0.;

	bad = nullptr;

	allocd.store(false, std::memory_order_relaxed);
	filled.store(false, std::memory_order_relaxed);
	do_gnuplot.store(false, std::memory_order_relaxed);

	mnm_rows =
	mnm_cols =
	mnm_ntot = 0;

	const parameter null_par = {"", "",
	                            0., 0., 0., 0., 0.,
	                            0, 0};

	for(parameter &x : fit_par_b)
		x = null_par;

	for(parameter &x : fit_par_p)
		x = null_par;
}

minime::~minime(void)
{
	if(load_AllocatedMemory())
	{
		free(data);
		if(bad != nullptr)
			free(bad);
	}

	#ifndef IGYBA_NDEBUG
	iprint(stdout, "'%s': memory released\n", __func__);
	#endif
}

void minime::set_Plotting(const bool do_it)
{
    store_UseGnuplot(do_it);
}

void minime::set_Wavelength(const double wlen)
{
	lambda = wlen;
}

double minime::get_Wavelength(void)
{
	return lambda;
}

void minime::set_PixelToUm(const double pix2um)
{
	scl = pix2um;
}

double minime::get_PixelToUm(void)
{
	return scl;
}

void minime::fill_DataFromFile(const std::string &fname,
							const std::string &bad_fname)
{
	if(!load_AllocatedMemory())
	{
		FILE *readfile = fopen(fname.c_str(), "r");
		if(readfile == NULL)
			file_error_msg(fname.c_str(), ERR_ARG);
		count_entries(readfile, &mnm_rows, &mnm_cols);
		fclose(readfile);

		if(!bad_fname.empty())
		{
			uint tx, ty;
			FILE *readfile = fopen(bad_fname.c_str(), "r");
			if(readfile == NULL)
				file_error_msg(bad_fname.c_str(), ERR_ARG);
			count_entries(readfile, &tx, &ty);
			fclose(readfile);
			if(tx != mnm_rows || ty != mnm_cols)
			{
				error_msg("wrong size of the bad pixel file. good bye.",
							ERR_ARG);
				exit(EXIT_FAILURE);
			}
		}

		data = alloc_mat(mnm_rows, mnm_cols);
		bad = alloc_ucharmatrix(mnm_rows, mnm_cols, 0);
		mnm_ntot = mnm_rows * mnm_cols;
		ffc.set_Dimensions(mnm_rows, mnm_cols);
		store_AllocatedMemory(true);
	}

	if(!load_FilledMemory())
	{
		fpfile2double(fname.c_str(), data, mnm_rows, mnm_cols);
		if(!bad_fname.empty())
			uintfile2uchar(bad_fname.c_str(), bad, mnm_rows, mnm_cols);

		store_FilledMemory(true);
	}
}

void minime::alloc_DataFromMemory(const uint nrows, const uint ncols,
								const uchar *const bad_in)
{
	if(!load_AllocatedMemory())
	{
		mnm_rows = nrows;
		mnm_cols = ncols;
		mnm_ntot = mnm_rows * mnm_cols;
		ffc.set_Dimensions(mnm_rows, mnm_cols);

		if(bad_in != nullptr)
			for(uint i = 0; i < mnm_ntot; ++i)
				if(bad_in[i] > 1)
					error_msg("the bad pixel matrix is ill conditioned",
								ERR_ARG);

		data = alloc_mat(mnm_rows, mnm_cols);
		bad = alloc_ucharmatrix(mnm_rows, mnm_cols, 0);

		store_AllocatedMemory(true);
	}
	else if((mnm_rows != nrows) || (mnm_cols != ncols))
	{
		mnm_rows = nrows;
		mnm_cols = ncols;
		mnm_ntot = mnm_rows * mnm_cols;
		ffc.set_Dimensions(mnm_rows, mnm_cols);

		if(bad_in != nullptr)
			for(uint i = 0; i < mnm_ntot; ++i)
				if(bad_in[i] > 1)
					error_msg("the bad pixel matrix is ill conditioned",
								ERR_ARG);

		data = realloc_mat(data, mnm_rows, mnm_cols);
		bad = realloc_ucharmatrix(bad, mnm_rows, mnm_cols, 0);
	}
	else
		warn_msg("why did you call me?", ERR_ARG);
}

void minime::fill_DataFromMemory(const double *res_pt data_in,
								const uchar *res_pt const bad_in)
{
	if(load_AllocatedMemory())
	{
		memcpy(data, data_in, mnm_ntot * sizeof(double));
		if(bad_in != nullptr)
			memcpy(bad, bad_in, mnm_ntot * sizeof(uchar));
		store_FilledMemory(true);
	}
}

void minime::get_CentroidBeamRadius(double *res_pt cen_x, double *res_pt cen_y,
									double *res_pt rad_x, double *res_pt rad_y,
									double *res_pt corr)
{
	double cx = 0.,
	       cy = 0.,
	       msum = 0.,
	       *work = alloc_mat(mnm_rows, mnm_cols);
	memcpy(work, data, mnm_rows * mnm_cols * sizeof(double));
	/** This section might be executed in parallel:
	 *
	 * #pragma omp parallel
	 * {
	 * 	#pragma omp for schedule(static) nowait
	 * 	for_1
	 * 	#pragma omp for schedule(static) nowait
	 * 	for_2
	 * }
	 *
	 * The question is whether it's useful here.
	 */
	for(uint i = 0; i < mnm_ntot; ++i)
		work[i] *= (1. - bad[i]);
	for(uint i = 0; i < mnm_rows; ++i)
		for(uint j = 0; j < mnm_cols; ++j)
		{
			const double f = work[i * mnm_cols + j];
			cx += f * i;
			cy += f * j;
			msum += f;
		}
	cx /= msum;
	cy /= msum;
	double rxx = 0., ryy = 0., rxy = 0.;
	for(uint i = 0; i < mnm_rows; ++i)
		for(uint j = 0; j < mnm_cols; ++j)
		{
			const double f = work[i * mnm_cols + j],
						 x = i - cx,
						 y = j - cy;
			rxx += f * x * x;
			ryy += f * y * y;
			rxy += f * x * y;
		}
	rxx /= msum;
	ryy /= msum;
	rxy /= msum;
	if(rxx <= 0.)
		error_msg("can't compute the radius, rxx is negative", ERR_ARG);
	if(ryy <= 0.)
		error_msg("can't compute the radius, ryy is negative", ERR_ARG);
	rxy *= 4.;
	const double rx = 2. * sqrt(rxx),
	             ry = 2. * sqrt(ryy);
	*cen_x = cx;
	*cen_y = cy;
	*rad_x = rx;
	*rad_y = ry;
	*corr = rxy / (rx * ry);
	if(fabs(*corr) >= 1.)
	{
		iprint(stderr, "corr: %g\n", *corr);
		error_msg("the correlation is >= 1, setting to unity", ERR_ARG);
		if(*corr > 0.)
			*corr = 1.;
		else
			*corr = -1.;
	}
	free(work);
}

void minime::get_CentroidBeamCovariance(double *res_pt cen_x,
										double *res_pt cen_y,
										double *res_pt wxx, double *res_pt wyy,
										double *res_pt wxy)
{
	double cx = 0., cy = 0., msum = 0.,
	       *work = alloc_mat(mnm_rows, mnm_cols);
	memcpy(work, data, mnm_rows * mnm_cols * sizeof(double));
	/** this section might be executed in parallel:
	 *
	 * #pragma omp parallel
	 * {
	 * 	#pragma omp for schedule(static) nowait
	 * 	for_1
	 * 	#pragma omp for schedule(static) nowait
	 * 	for_2
	 * }
	 *
	 * question is whether it's useful here - probably
	 */
	for(uint i = 0; i < mnm_ntot; ++i)
		work[i] *= (1. - bad[i]);
	for(uint i = 0; i < mnm_rows; ++i)
		for(uint j = 0; j < mnm_cols; ++j)
		{
			const double f = work[i * mnm_cols + j];
			cx += f * i;
			cy += f * j;
			msum += f;
		}
	cx /= msum;
	cy /= msum;
	double rxx = 0., ryy = 0., rxy = 0.;
	for(uint i = 0; i < mnm_rows; ++i)
		for(uint j = 0; j < mnm_cols; ++j)
		{
			const double f = work[i * mnm_cols + j],
						 x = i - cx,
						 y = j - cy;
			rxx += f * x * x;
			ryy += f * y * y;
			rxy += f * x * y;
		}
	free(work);
	rxx /= msum;
	ryy /= msum;
	rxy /= msum;
	if(rxx <= 0.)
		error_msg("can't compute the radius, rxx is negative", ERR_ARG);
	if(ryy <= 0.)
		error_msg("can't compute the radius, ryy is negative", ERR_ARG);

	rxx *= 4.;
	ryy *= 4.;
	rxy *= 4.;

	*cen_x = cx;
	*cen_y = cy;
	*wxx = rxx;
	*wyy = ryy;
	*wxy = rxy;
}

/** \brief Computes a Gaussian beam intensity according to the global beam
 * radii and the correlation between them.
 *
 * \param wxz const double The global beam radius in x.
 * \param wyz const double The global beam radius in y.
 * \param corr const double The correlation between wxz and wyz.
 * \param x_off const double The centroid in x.
 * \param y_off const double The centroid in y
 * \param out double* The output array.
 * \return void
 *
 */
void minime::get_GaussBeamMultinormal(const double wxz, const double wyz,
									const double corr,
									const double x_off, const double y_off,
									double *out)
{
	const double sx = wxz,
	             sxx = sx * sx,
	             sy = wyz,
	             syy = sy * sy,
	             sxy = corr * sx * sy,
	             t_n = sxx * syy * (1. - corr * corr);
	if(fabs(corr) < 1.)
	{
		const double norm = 2. / (M_PI * sx * sy * sqrt(1. - corr * corr));
		#pragma omp parallel num_threads(2) \
		firstprivate(norm, sxx, syy, t_n, sxy) shared(out)
		{
			#pragma omp for schedule(static)
			for(uint x = 0; x < mnm_rows; ++x)
			{
				const uint xcols = x * mnm_cols;
				const double ox = x - x_off,
							 oxx_yy = ox * ox * syy;
				for(uint y = 0; y < mnm_cols; ++y)
				{
					const double oy = y - y_off,
								 oyy_xx = oy * oy * sxx,
								 oxy_xy = 2. * ox * oy * sxy,
								 e = (oxx_yy + oyy_xx - oxy_xy) / t_n;
					out[xcols + y] = norm * exp(-2. * e);
				}
			}
		}
	}
	else
		for(uint i = 0; i < mnm_ntot; ++i)
			out[i] = 0.;
}

void minime::get_GaussBeamMultinormalCovar(const double sxx, const double syy,
										const double sxy,
										const double x_off, const double y_off,
										double *out)
{
	const double t_n = sxx * syy - sxy * sxy,
	             t_oy_xy = y_off * sxy,
	             t_oy_xx = y_off * sxx,
	             t_ox_xy = x_off * sxy,
	             t_ox_yy = x_off * syy;
	if(t_n <= 0.)
	{
		const double norm = 2. / (M_PI * sqrt(t_n));
		#pragma omp parallel num_threads(2) \
		firstprivate(norm, sxx, syy, sxy, \
					t_n, t_oy_xy, t_oy_xx, t_ox_xy, t_ox_yy) \
		shared(out)
		{
			#pragma omp for schedule(static)
			for(uint x = 0; x < mnm_rows; ++x)
			{
				const double ox = x - x_off,
							 syy_x = syy * x,
							 sxy_x = sxy * x;
				for(uint y = 0; y < mnm_cols; ++y)
				{
					const double oy = y - y_off,
								 sxx_y = sxx * y,
								 sxy_y = sxy * y,
								 expy = oy *
								        (t_oy_xx - t_ox_xy + sxy_x - sxx_y),
					             expx = -ox *
					                    (t_oy_xy - t_ox_yy + syy_x - sxy_y),
								 e_tot = 2. * (expx + expy) / t_n;
					out[x * mnm_cols + y] = norm * exp(e_tot);
				}
			}
		}
	}
	else
		for(uint i = 0; i < mnm_ntot; ++i)
			out[i] = 0.;
}

void minime::store_AllocatedMemory(const bool b)
{
	allocd.store(b, std::memory_order_release);
}

bool minime::load_AllocatedMemory(void)
{
	return allocd.load(std::memory_order_acquire);
}

void minime::store_FilledMemory(const bool b)
{
	filled.store(b, std::memory_order_release);
}

bool minime::load_FilledMemory(void)
{
	return filled.load(std::memory_order_acquire);
}

void minime::store_UseGnuplot(const bool b)
{
	do_gnuplot.store(b, std::memory_order_release);
}

bool minime::load_UseGnuplot(void)
{
	return do_gnuplot.load(std::memory_order_acquire);
}
