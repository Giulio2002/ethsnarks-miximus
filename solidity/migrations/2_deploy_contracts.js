const Verifier = artifacts.require('Verifier.sol');
const TestableMiximus = artifacts.require('TestableMiximus.sol');
const MiMC = artifacts.require('MiMC.sol');
const MerkleTree = artifacts.require('MerkleTree.sol');


let list_flatten = (l) => {
    return [].concat.apply([], l);
};


let vk_to_flat = (vk) => {
    return [
        list_flatten([
            vk.alpha[0], vk.alpha[1],
            list_flatten(vk.beta),
            list_flatten(vk.gamma),
            list_flatten(vk.delta),
        ]),
        list_flatten(vk.gammaABC)
    ];
};


async function doDeploy( deployer, network )
{
    await deployer.deploy(MiMC);
	await deployer.deploy(Verifier);
	await deployer.link(Verifier, TestableMiximus);
    await deployer.link(MiMC, TestableMiximus);

    var vk = require('../../.keys/miximus.vk.json');
    let [vk_flat, vk_flat_IC] = vk_to_flat(vk);
	await deployer.deploy(TestableMiximus,
		vk_flat,
		vk_flat_IC
		);
}


module.exports = function (deployer, network) {
	deployer.then(async () => {
		await doDeploy(deployer, network);
	});
};
