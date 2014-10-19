using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using MultiWorldTesting;
using System.Diagnostics;

namespace E4Lab
{
    class Analysis
    {
        private MwtLogger interactionLogger;
        private RewardStore rewardStore;
        private MwtOptimizer mwtOptimizer;
        private const int numActions = 8;
        private INTERACTION[] fullInteractions;
       
        public Analysis(string interactionLogFile, string rewardStoreFile)
        {
            interactionLogger = new MwtLogger(interactionLogFile);
            rewardStore = new RewardStore(rewardStoreFile);
        }

        public void joinRewardsAndInteractions() 
        {
            //Start with partial interactions
            INTERACTION[] partialInteractions = interactionLogger.GetAllInteractions();

            MwtRewardReporter mwtRewardReporter = new MwtRewardReporter(partialInteractions);

            //Retrieve logged rewards from the store
            float[] rewards = rewardStore.GetAllRewards();
            float rewardSum = 0.0f;

            //Join partial interactions and rewards
            for (uint iInter = 0; iInter < partialInteractions.Length; iInter++)
            {
                bool found = mwtRewardReporter.ReportReward(partialInteractions[iInter].Id, rewards[iInter]);
                rewardSum += rewards[iInter];
                if (!found)
                    Console.WriteLine("DID NOT FIND ID:{0}", partialInteractions[iInter].Id);
            }

            //Getting full interactions
            fullInteractions = mwtRewardReporter.GetAllInteractions();
        }

        //Code to evaluate a policy with a threshold parameter
        public void Evaluate(float policyParam)
        {
            if(mwtOptimizer == null)
                mwtOptimizer = new MwtOptimizer(fullInteractions, numActions);

             
            float val = mwtOptimizer.EvaluatePolicy<float>(new StatefulPolicyDelegate<float>(PolicyClass.policyFunc), policyParam);

            Console.WriteLine("Evaluating policy with parameter = {0}, obtained reward = {1}", policyParam, val);
        }

        //Code to learn from the interactions using Vowpal Wabbit and evaluate the learned policy
        public void Optimize()
        {
            if (mwtOptimizer == null)
            {
                  
                Console.WriteLine("Initializing opt");
                mwtOptimizer = new MwtOptimizer(fullInteractions, numActions);
            }

            Console.WriteLine("Now we will optimize");
            Stopwatch sw = Stopwatch.StartNew();
            mwtOptimizer.OptimizePolicyVWCSOAA("model");
            sw.Stop();
            TimeSpan elapsedTime = sw.Elapsed;

            Console.WriteLine("Time spent in optimizer = {0}", elapsedTime.ToString());
            Console.WriteLine("Done with optimization, now we will evaluate the optimized model");
            Console.WriteLine("Value of optimized policy using VW = {0}", mwtOptimizer.EvaluatePolicyVWCSOAA("model"));
        }
    }
}
