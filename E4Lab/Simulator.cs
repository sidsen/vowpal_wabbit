using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using MultiWorldTesting;

namespace E4Lab
{
    
    class Simulator
    {
        private const string contextFile = "speller-contexts";
        private const string rewardFile = "speller-rewards";
        private IOUtils iou;
        private MwtLogger interactionLogger;
        private RewardStore rewardStore;
        private float defaultPolicyParam;
        
        public Simulator(string interactionLogFile, string rewardStoreFile, float policyParam)
        {
            iou = new IOUtils(contextFile, rewardFile);
            interactionLogger = new MwtLogger(interactionLogFile);
            rewardStore = new RewardStore(rewardStoreFile);
            defaultPolicyParam = policyParam;
        }

        public void Run() 
        {
            Console.WriteLine("Initializing Service");            

            MyService myService = new MyService(interactionLogger, defaultPolicyParam);
            CONTEXT c;
            List<float> rewardList = new List<float>();

            Console.WriteLine("Starting simulation");

            float rewardSum = 0.0f;

            while ((c = iou.getContext()) != null)
            {
                Tuple<uint, uint> action_and_id = myService.ProcessRequest(c);
                float reward = iou.getReward(action_and_id.Item1, action_and_id.Item2);
                rewardStore.Add(reward);
                rewardSum += reward;
            }

            Console.WriteLine("Average reward while exploration = {0}", rewardSum / rewardStore.GetAllRewards().Length);

            //hand over the interaction logs to the logger
            myService.mwt.GetAllInteractions();
            
        }

        public void ShutDown()
        {
            Console.WriteLine("Saving {0} interactions", interactionLogger.GetAllInteractions().Length);
            interactionLogger.Flush();
            rewardStore.GetAllRewards();
            rewardStore.Flush();                           
            
        }

        //public static void Main()
        //{
        //    Simulator simulator = new Simulator();
        //    simulator.Run();
        //    Console.WriteLine("Finished simulation");
        //    simulator.ShutDown();
        //    Console.WriteLine("Finished shutdown");
        //    Console.ReadKey();
        //}
    }
}
