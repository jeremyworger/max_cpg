/// @file	
///	@ingroup 	CPG
/// @author		Dan Bennett
///	@license	Usage of this file and its contents is governed by the MIT License


/*
	No Quantiser on this one

	TODO:

		- write new operator
		- add controls for 
			- delay out change

		Does weight scaling work correctly?


	LONGER TERM:
		External input(s)
		Smoothing control changes
		Waveshaping between connections


*/



#include "c74_min.h"
#include "matsuokaEngine.h"
#include "matsuNode.h"

#define CALIBRATION_CYCLES 20
#define FREQ_MIN 0.001
#define MAX_NODES 16
#define INTERP_SAMPLES 4
#define UNITY_CONN_WEIGHT 1
#define P_TRATIO 4
#define P_C 1
#define P_B 4.07
#define P_G 4.07




using namespace c74::min;

class cpg_net : public object<cpg_net>, public vector_operator<>
{
private:
	std::shared_ptr<MatsuokaEngine> engine_ptr;
	MatsuNode dummyNode;
	double freqComp{ DEFAULTFREQCOMPENSAITON };
	int local_srate;

	// holds raw output values for interpolation. Barebones ringbuffer approach.
	sample outRingBuff[MAX_NODES][INTERP_SAMPLES]{ 0 };
	int ringIndex{ 0 };
	number freqs[MAX_NODES]{ 0 };
	number phase{ 0 };
	number phaseStep;

	lib::interpolator::hermite<sample> interp_herm;
	lib::interpolator::linear<sample> interp_lin;
	bool m_initialized{ false };
	int nodeCount{ 1 };

	vector< unique_ptr<inlet<>> >			inlets;				///< this object's inlets
	vector< unique_ptr<outlet<>> >			outlets;				///< this object's outlets

public:


	MIN_DESCRIPTION{ "A basic, no frills Matsuoka Oscillator node" };
	MIN_TAGS{ "audio, oscillator" };
	MIN_AUTHOR{ "Cycling '74" };
	MIN_RELATED{ "phasor~" };


	// Because our object defines a constructor (below) these argument definitions are for
	// documentation purposes only.
	argument<int> node_count_arg{ this, "node_count", "The number of nodes in the network." };
	argument<int> sample_rate_arg{ this, "sample_rate", "The sample rate of network calculations." };
	argument<int> tRatio_arg{ this, "tRatio_param", "The ratio of equation params t2:t1." };
	argument<int> c_arg{ this, "c_param", "The equation parameter c." };
	argument<int> b_arg{ this, "c_param", "The equation parameter b." };
	argument<int> g_arg{ this, "c_param", "The equation parameter g." };



	cpg_net(const atoms& args = {})
	{
		m_initialized = false;

		cout << "started" << endl;

		// gather info for basic object init
		if (args.size() > 0) {
			nodeCount = args[0];
		}

		if (args.size() > 1) {
			unsigned srate_in = (unsigned)(int)args[1];
			cout << "sample rate requested: " << srate_in << endl;
			if ((int)srate_in > samplerate() / 2 || (int)srate_in < 0) {
				cout << "sample rate must be system sample rate, or else >0 and < system sample-rate / 2: " << srate_in << endl;
				cout << "setting to system sample rate" << endl;

				local_srate = (int)samplerate();
			}
			else {
				local_srate = srate_in;
			}
		}
		else {
			local_srate = (int)samplerate();
		}

		engine_ptr = std::shared_ptr<MatsuokaEngine>(
			new  MatsuokaEngine(local_srate, true, false, true));
		dummyNode = MatsuNode();

		// will be adding more (replacing this) later
		inlets.push_back(std::make_unique<inlet<>>(this, "(messages) control input"));

		// set up nodes and inlets/outlets for them
		for (int nodeID = 0; nodeID < nodeCount; ++nodeID) {
			//inlets.push_back(std::make_unique<inlet<>>(this, "(signal) audio input"));
			outlets.push_back(std::make_unique<outlet<>>(this, "(signal) audio output " + nodeID, "signal"));
			if (nodeID != 0) {
				engine_ptr->addChild(0, nodeID);
			}
		}
		// connect all nodes to all others, but with 0 signal weight
		for (int nodeID = 0; nodeID < nodeCount; ++nodeID) {
			for (int connectToID = 0; connectToID < nodeCount; ++connectToID) {
				if (nodeID != connectToID) {
					engine_ptr->setConnection(nodeID, connectToID, 0.0);
				}
			}
		}

		engine_ptr->setUnityConnectionWeight(UNITY_CONN_WEIGHT);
		engine_ptr->setConnectionWeightScaling(true);
		engine_ptr->setParam_t2Overt1(T2_INIT / T1_INIT);
		engine_ptr->setParam_c(C_INIT);
		engine_ptr->setParam_b(B_INIT);
		engine_ptr->setParam_g(G_INIT);
		engine_ptr->setFreqCompensation(DEFAULTFREQCOMPENSAITON);
		engine_ptr->setNodeFrequency(0u, 1.0, true);

		dummyNode.set_t1(T1_INIT);
		dummyNode.set_t2(T2_INIT);
		dummyNode.set_c(C_INIT);
		dummyNode.set_b(B_INIT);
		dummyNode.set_g(G_INIT);

		setParams(args, true);

		engine_ptr->calibrate();
		calibrate.set();

		cout << "initialised network" << endl;

		m_initialized = true;
	}


	inlet<>			in{ this, "(signal) input" };
	inlet<>			freq{ this, "(signal) node frequency" };

	outlet<>		out{ this, "(signal) ramp wave", "signal" };


	//argument<number> a_t1 { this, "t1", "equation parameter t1",
	//	MIN_ARGUMENT_FUNCTION {
	//		node.set_t1( arg);
	//	}
	//};


	message<> bang{ this, "bang", "reset the node",
		MIN_FUNCTION{
		if (m_initialized) {
			engine_ptr->reset();
		}
	return {};
	}
	};

	message<> params{ this, "params",
		MIN_FUNCTION{
		setParams(args, false);
	calibrate.set();
	return {};
	}
	};


	message<> weight{ this, "weight",
		MIN_FUNCTION{
		if (args.size() >= 3) {
			engine_ptr->setConnection((int)args[0],(int)args[1], (double)args[2]);
			}
	return {};
	}
	};

	message<> offset{ this, "offset",
		MIN_FUNCTION{
		if (args.size() >= 3) {
			engine_ptr->setConnectionPhaseOffset((int)args[0],(int)args[1], (double)args[2]);
		}
	return {};
	}
	};

	message<> node_freq{ this, "node_freq",
		MIN_FUNCTION{
		if (args.size() >= 2){
			freqs[(int)args[0]] = args[1];
			engine_ptr->setNodeFrequency((int)args[0],(int)args[1], false);
		}
	return {};
	}
	};

	message<> net_freq{ this, "net_freq",
		MIN_FUNCTION{
		if (!args.empty()) {
			number freqMult = (float)args[0] / freqs[0];
			freqs[0] = args[0];
			engine_ptr->setNodeFrequency(0,(int)args[0], true);
			for (int i = 1; i < nodeCount; i++) {
				freqs[i] *= freqMult;
			}
		}
	return {};
	}
	};

	queue calibrate{ this,
		MIN_FUNCTION{
		doCalibration();
	return {};
	}
	};



	//attribute<number> frequency { this, "frequency" , 1.0, description{"Frequency in Hz"},
	//	setter { MIN_FUNCTION {
	//		if (m_initialized) {
	//			m_oscillator.frequency(args[0], samplerate());
	//		}
	//		return args;
	//	}}
	//};




	void calcVector_nonInterp(audio_bundle input, audio_bundle output)
	{
		// For each frame in the vector calc each channel

		for (auto frame = 0; frame<input.frame_count(); ++frame) {
			engine_ptr->doQueuedActions();
			engine_ptr->step();
			// send to max output
			for (int channel = 0; channel < nodeCount; ++channel) {
				output.samples(channel)[frame] = outRingBuff[channel][ringIndex];
			}
		}
	}



	void calcVector_interp(audio_bundle input, audio_bundle output)
	{
		// For each frame in the vector calc each channel

		for (auto frame = 0; frame<input.frame_count(); ++frame) {

			// if phase wraps, calc sample from network
			phase += phaseStep;
			if (phase > 1.0) {

				ringIndex++;
				if (ringIndex >= INTERP_SAMPLES) { ringIndex = 0; }
				engine_ptr->doQueuedActions();
				engine_ptr->step();
			}
			else if (phase < 0.0) { // sholdn't happen
				phase += 1.0;
			}

			// regardless of phase wrap, calculate our interpolated sample for outputs
			for (int channel = 0; channel < nodeCount; ++channel) {
				outRingBuff[channel][ringIndex] = engine_ptr->getNodeOutput(channel);

				if (local_srate > 11024) {
					output.samples(channel)[frame] =
							interp_herm(interpRingLookup(outRingBuff[channel], ringIndex,-3),
										interpRingLookup(outRingBuff[channel], ringIndex, -2),
										interpRingLookup(outRingBuff[channel], ringIndex, -1),
										outRingBuff[channel][ringIndex],
										phase);
				}
				else {
					output.samples(channel)[frame] =
						interp_lin(interpRingLookup(outRingBuff[channel], ringIndex, -3),
							interpRingLookup(outRingBuff[channel], ringIndex, -2),
							interpRingLookup(outRingBuff[channel], ringIndex, -1),
							outRingBuff[channel][ringIndex],
							phase);
				}
				
			}
		}
	}



	/// Process N channels of audio
	/// Max takes care of squashing denormal for us by setting the FTZ bit on the CPU.
	void operator()(audio_bundle input, audio_bundle output) 
	{
		if (m_initialized) {
			phaseStep = local_srate / samplerate();

			if (local_srate == (int)samplerate()) {
				calcVector_nonInterp(input, output);
			}
			else {
				calcVector_interp(input, output);
			}
		}

	}

	using vector_operator::operator();


	//void calibrate()
	//{
	//	// calibrate nodes 
	//	int settleTime = local_srate * 2;
	//	dummyNode.setFrequency(CALIBRATION_CYCLES, local_srate);

	//	while (settleTime-- > 0) {
	//		dummyNode.doCalcStep(true, true);
	//		node.doCalcStep(true, true);
	//	}
	//	freqComp = dummyNode.calcFreqCompensation(CALIBRATION_CYCLES, local_srate);
	//	node.setFreqCompensation(freqComp);
	//	node.setFrequency(_freq, local_srate);
	//}




	void doCalibration()
	{
		m_initialized = false;
		// calibrate nodes 
		int settleTime = local_srate * 2;
		dummyNode.setFrequency(CALIBRATION_CYCLES, local_srate);
		dummyNode.reset();
		while (settleTime-- > 0) {
			dummyNode.doCalcStep(true, true);
		}
		freqComp = dummyNode.calcFreqCompensation(CALIBRATION_CYCLES, local_srate);

		engine_ptr->setFreqCompensation(freqComp);

		for (int i = 0; i < nodeCount; i++) {
			engine_ptr->setNodeFrequency(i, freqs[i], false);
		}
		engine_ptr->reset();
	
		m_initialized = true;
	}


	void setParams(const atoms& args, bool startup)
	{

		int firstParam = startup ? 2 : 0;

		if (args.size() > firstParam) {
			engine_ptr->setParam_t2Overt1(args[firstParam]);
			dummyNode.set_t2_over_t1(args[firstParam]);
		}
		if (args.size() > firstParam + 1) {
			engine_ptr->setParam_c(args[firstParam + 1]);
			dummyNode.set_c(args[firstParam + 1]);
		}

		if (args.size() > firstParam + 2) {
			engine_ptr->setParam_b(args[firstParam + 2]);
			dummyNode.set_b(args[firstParam + 2]);
		}

		if (args.size() > firstParam + 3) {
			engine_ptr->setParam_g(args[firstParam + 3]);
			dummyNode.set_g(args[firstParam + 3]);
		}
	}

	// Helper function for our barebones ring buffer approach
	// CAREFUL: offset must be negative - 
	// adding branch here to protect against invalid values would be a waste of resource
	sample interpRingLookup(sample samps[INTERP_SAMPLES], int curr, int offset)
	{
		int raw = curr + offset;
		return samps[raw < 0 ? raw + INTERP_SAMPLES : raw];
	}

};

MIN_EXTERNAL(cpg_net);