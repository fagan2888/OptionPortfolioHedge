#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <omp.h>
#include <time.h>
#include <iostream>
#include <fstream>


#ifndef Pi 
#define Pi 3.141592653589793238462643 
#endif

#define sqrt2pi 2.506628 // sqrt(2 * Pi)

enum optionType {
	otEuroCall,
	otEuroPut
};

float rnorm() {
	float u = ((float)rand() / (RAND_MAX)) * 2 - 1;
	float v = ((float)rand() / (RAND_MAX)) * 2 - 1;
	float r = u * u + v * v;
	if (r == 0 || r > 1) return rnorm();
	float c = sqrt(-2 * log(r) / r);
	return u * c;
}

// M�todo que retorna um vetor com um movimento browniano.
float* MB(size_t n, float T) {

	// Modificar o seed do gerador aleat�rio
	(srand((unsigned)time(NULL)));

	float h = T / n;
	float sh = sqrt(h);
	float r;

	float* B = (float *)calloc(n + 1, sizeof(float));

	size_t i;
	for (i = 0; i < n; i++)
	{
		r = rnorm() * sh;
		B[i + 1] = B[i] + r;
	}

	return B;
}

float pnorm(float x) {

	float L, K, w;
	/* constants */
	double const a1 = 0.31938153, a2 = -0.356563782, a3 = 1.781477937;
	double const a4 = -1.821255978, a5 = 1.330274429;

	L = fabs(x);
	K = 1.0 / (1.0 + 0.2316419 * L);
	w = 1.0 - 1.0 / sqrt(2 * Pi) * exp(-L *L / 2) * (a1 * K + a2 * K *K + a3 * K*K*K + a4 * K*K*K*K + a5 * K*K*K*K*K);

	if (x < 0){
		w = 1.0 - w;
	}

	return w;
}

// Retorna um vetor com a discretiza��o do tempo
float* seq(float a, float b, float n) {

	float *x = (float*)calloc(n, sizeof(float));
	float by = (b - a) / n;


	for (size_t i = 0; i < n; i++)
	{
		x[i] = a + by * i;
	}

	return x;
}

// Calcula os valores de d1 e d2 da f�rmula de BS
float* bs_d(float T, float t, float S0, float K, float r, float sigma) {

	float sigmaSqrtT = sigma * sqrt(T - t);
	float *d = (float*)calloc(2, sizeof(float));
	d[0] = (log(S0 / K) + (r + 0.5*sigma*sigma) * (T - t)) / (sigmaSqrtT);
	d[1] = d[0] - sigmaSqrtT;

	return d;
}

// Calcula o pre�o de uma op��o
float preco_bs(float T, float t, float S0, float K, float r, float sigma, optionType opcao) {

	float *d = bs_d(T, t, S0, K, r, sigma);

	switch (opcao)
	{
	case otEuroCall:
		return pnorm(d[0]) * S0 - pnorm(d[1]) * K * exp(-r * (T - t));
		break;
	case otEuroPut:
		return pnorm(-d[1]) * K * exp(-r * (T - t)) - pnorm(-d[0]) * S0;
		break;
	default:
		return 0.0;
		break;
	}
	free(d);
}

// Gera uma simula��o da evolu��o do pre�o do ativo, j� descontado na medida f�sica.
void evolucao_real_bs(float T, float S0, float r, float sigma, size_t n, float mu, float *Tempos, float *S) {

	float *W;
	
	W = MB(n, T);
	for (size_t i = 0; i < n; i++)
	{
		S[i] = S0 * exp((mu - r - sigma*sigma / 2) * Tempos[i] + sigma * W[i]);
	}

	free(W);
}

// C�lculo da quantidade do ativo objeto da op��o para realizar o delta hedging.
float hedging_bs(float T, float t, float S0, float K, float r, float sigma, optionType opcao) {

	float *d = bs_d(T, t, S0, K, r, sigma);

	switch (opcao)
	{
	case otEuroCall:
		return pnorm(d[0]);
		break;
	case otEuroPut:
		return -pnorm(-d[0]);
		break;
	default:
		return 0.0;
		break;
	}
	free(d);
}

// C�lculo do payoff da op��o trazido a valor presente.
float payoff_descontado(float T, float r, float ST, float K, optionType opcao) {

	float payoff;

	switch (opcao)
	{
	case otEuroCall:
		payoff = ST - K * exp(-r * T);
		break;
	case otEuroPut:
		payoff = K * exp(-r * T) - ST;
		break;
	default:
		break;
	}

	if (payoff < 0)
		return 0.0;
	else
		return payoff;

}

// Econtra o maior indice do vetor f que possui os valores menores que x.
size_t which(float* f, size_t n, float x) {

	size_t i = 0;
	while ((f[i] < x) && (i < n))
	{
		i++;
	}

	return i;
}

struct HedingErrorParameters{
	float T; // Vencimento da op��o
	size_t M; // N�mero de simula��es
	size_t n; // Quantidade de intervalos gerados no tempo T para realizar a discretiza��o
	float mu; // taxa m�dia de retorno
	float S0; // Pre�o inicial da a��o 
	float r; // taxa de juros livre de risco
	float sigma; // volatilidade BS
	optionType opcao; // tipo de op��o
	float K; // strike da op��o
	size_t l; // quantidade de rebalanceamentos de delta hedging que ser� realizado no portfolio.
};

struct HedingErrorResult{
	float error;
	double elapsedTime;
};

HedingErrorResult CalculaHedgingError(HedingErrorParameters p){
	double t1 = omp_get_wtime();

	float T = p.T;
	size_t M = p.M;
	size_t n = p.n;
	float mu = p.mu;
	float S0 = p.S0;
	float r = p.r;
	float sigma = p.sigma;
	optionType opcao = p.opcao;
	float K = p.K;
	size_t l = p.l;

	float *erro = (float*)calloc(M, sizeof(float));
	float *Tempos = seq(0.0, T, n);
	size_t *tempoDeHedging = (size_t*)calloc(l, sizeof(size_t));
	float preco_opcao = preco_bs(T, 0.0, S0, K, r, sigma, opcao);

	// Gera um vetor para sabermos os momentos em Tempos que ser�o feitos os delta hedgings.
	for (size_t j = 0; j < l; j++) {
		float tempo = (float)j / (float)l;
		tempoDeHedging[j] = which(Tempos, n, tempo);
	}

#pragma omp parallel for
	for (int i = 0; i < M; i++)
	{
		float *S_int = (float*)calloc(l + 1, sizeof(float));
		float *S = (float*)calloc(n, sizeof(float));
		float *hedging = (float*)calloc(l, sizeof(float));
		float Pay_descontado_real = 0;
		float integral = 0, valor_portfolio_final = 0;

		// Gera uma trajet�ria do pre�o da a��o que ser� utilizada como a trajet�ria real para c�lculo do resultado da carteira.
		evolucao_real_bs(T, S0, r, sigma, n, mu, Tempos, S);
		Pay_descontado_real = payoff_descontado(T, r, S[n - 1], K, opcao);

		integral = 0;
		S_int[0] = S[tempoDeHedging[0]];
		hedging[0] = hedging_bs(T, Tempos[tempoDeHedging[0]], S_int[0], K, r, sigma, opcao);
		for (size_t j = 1; j < l; j++) {
			S_int[j] = S[tempoDeHedging[j]];
			hedging[j] = hedging_bs(T, Tempos[tempoDeHedging[j]], S_int[j], K, r, sigma, opcao);
			integral += hedging[j - 1] * (S_int[j] - S_int[j - 1]);
		}
		S_int[l] = S[n - 1];
		integral += hedging[l - 1] * (S_int[l] - S_int[l - 1]);

		valor_portfolio_final = preco_opcao + integral;
		erro[i] = Pay_descontado_real - valor_portfolio_final;

		free(S);
		free(hedging);
		free(S_int);
	}

	float hedging_error = 0;
	for (size_t i = 0; i < M; i++)
		hedging_error += (erro[i] / M);

	free(Tempos);
	free(erro);
	free(tempoDeHedging);

	HedingErrorResult result;
	result.error = hedging_error;
	result.elapsedTime = omp_get_wtime() - t1;

	return result;
}

int main(int argc, char *argv[]){
	
	HedingErrorParameters p;
	HedingErrorResult r;

	FILE *pfile;
	fopen_s(&pfile, "output.txt", "w");
	fprintf(pfile, "HedgingError,ElapsedTime\n");

	p.T = 1;
	p.M = 10000;
	p.n = 200;
	p.mu = 0.01;
	p.S0 = 100;
	p.r = 0;
	p.sigma = 0.09;
	p.opcao = otEuroCall;
	p.K = 100;
	p.l = 30;	

	for (int i = 0; i < 50; i++){
		r = CalculaHedgingError(p);
		fprintf(pfile, "%f,%f\n", r.error, r.elapsedTime);
	}

	fclose(pfile);

	printf("Press [ENTER] to exit.");
	int c = fgetc(stdin);

	return 0;
}