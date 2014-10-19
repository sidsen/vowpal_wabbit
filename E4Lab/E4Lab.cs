using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using MultiWorldTesting;

namespace E4Lab
{
    class E4Lab
    {
        public string interactionLogFile = "interaction-logs";
        public string rewardStoreFile = "reward-store";
        public bool Explore = true;
        public bool Analyze = true;        

        public void simulateExploration()
        {
            float defaultPolicyParam = 0.1f;
            Simulator simulator = new Simulator(interactionLogFile, rewardStoreFile, defaultPolicyParam);
            simulator.Run();
            Console.WriteLine("Finished simulation");
            simulator.ShutDown();            
            //Console.WriteLine("Finished shutdown. Press any key to continue...");
            //Console.ReadKey();
        }

        public static void Main()
        {            
            E4Lab lab = new E4Lab();
            // First simulate the exploration and collection of rewards
            if (lab.Explore)
                lab.simulateExploration();

            //Now we analyze the data we have collected

            if (lab.Analyze)
            {
                Analysis analysis = new Analysis(lab.interactionLogFile, lab.rewardStoreFile);

                //  Joining the exploration data with reward information
                analysis.joinRewardsAndInteractions();
                
                //Let us first evaluate the default policy
                float policyParam = 0.1f;
                analysis.Evaluate(policyParam);

                for (policyParam = 0.05f; policyParam < 1; policyParam += 0.05f)
                    analysis.Evaluate(policyParam);

                //Console.WriteLine("Press any key to continue...");
                //Console.ReadKey();

                //Let us optimize
                analysis.Optimize();

                //Console.WriteLine("Press any key to continue...");
                //Console.ReadKey();
            }
        }
    }
}
